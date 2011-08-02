/* ex: set ts=4 et: */
/*
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <math.h>
#include <wand/MagickWand.h>

#if defined(_POSIX_MAPPED_FILES) && _POSIX_MAPPED_FILES > 0
#include <sys/mman.h>
#define IMGMIN_USE_MMAP
#endif

/*
 * The most important threshold; the amount of change we're
 * will to accept
 * very conservative: 0.50
 * conservative:      0.75
 * sweet spot:        1.00
 */
#define CMP_THRESHOLD              1.00

/*
 *
 */
#define COLOR_DENSITY_RATIO        0.11

/*
 * avoid low-color images like gradients
 */
#define MIN_UNIQUE_COLORS       4096

#define QUALITY_OUT_MAX               95
#define QUALITY_OUT_MIN               70
#define QUALITY_IN_MIN                82

/*
 * never perform more than this many steps.
 * trades potential quality improvements for reduced runtime,
 * useful for low latency or very large batch runs
 */
#define MAX_ITERATIONS             5

#define ThrowWandException(wand)                                \
{                                                               \
    char *description;                                          \
    ExceptionType severity;                                     \
    description = MagickGetException(wand, &severity);          \
    (void) fprintf(stderr,"%s %s %lu %s\n",                     \
      GetMagickModule(),description);                           \
    description = (char *) MagickRelinquishMemory(description); \
    exit(-1);                                                   \
}

struct imgmin_options
{
    double   error_threshold,
             color_density_ratio;
    unsigned min_unique_colors,
             quality_out_max,
             quality_out_min,
             quality_in_min,
             max_iterations;
};

static size_t unique_colors(MagickWand *mw)
{
    return MagickGetImageColors(mw);
}

static unsigned long quality(MagickWand *mw)
{
    return GetImageFromMagickWand(mw)->quality;
}

static double color_density(MagickWand *mw)
{
    const size_t area = MagickGetImageHeight(mw) * MagickGetImageWidth(mw);
    double density = (double)unique_colors(mw) / area;
    return density;
}

#define min(a,b) ((a) < (b) ? (a) : (b))
#define max(a,b) ((a) > (b) ? (a) : (b))

static const char * type2str(const ImageType t)
{
    return 
           t == UndefinedType            ? "Undefined"            :
           t == BilevelType              ? "Bilevel"              :
           t == GrayscaleType            ? "Grayscale"            :
           t == GrayscaleMatteType       ? "GrayscaleMatte"       :
           t == PaletteType              ? "Palette"              :
           t == PaletteMatteType         ? "PaletteMatte"         :
           t == TrueColorType            ? "TrueColor"            :
           t == TrueColorMatteType       ? "TrueColorMatte"       :
           t == ColorSeparationType      ? "ColorSeparation"      :
           t == ColorSeparationMatteType ? "ColorSeparationMatte" :
           t == OptimizeType             ? "Optimize"             :
           t == PaletteBilevelMatteType  ? "PaletteBilevelMatte"  :
           "???";
}

static MagickWand * search_quality(MagickWand *mw, const char *dst,
                                   const struct imgmin_options *opt)
{
    MagickWand *tmp = NULL;

    if (unique_colors(mw) < opt->min_unique_colors && MagickGetType(mw) != GrayscaleType)
    {
        fprintf(stderr, " Color count is too low, skipping...\n");
        return CloneMagickWand(mw);
    }

    if (quality(mw) < opt->quality_in_min)
    {
        fprintf(stderr, " Quality < %u, won't second-guess...\n", opt->quality_in_min);
        return CloneMagickWand(mw);
    }

    {
        ExceptionInfo *exception = AcquireExceptionInfo();

        double original_density = color_density(mw);
        unsigned qmax = min(quality(mw), opt->quality_out_max);
        unsigned qmin = max(opt->quality_out_max - (1 << MAX_ITERATIONS), opt->quality_out_min);

#ifndef CompositeChannels
#define CompositeChannels 0x2f
#endif

        while (qmax > qmin + 2)
        {
            unsigned q;

            q = (qmax + qmin) / 2;
            tmp = CloneMagickWand(mw);
            MagickSetImageCompressionQuality(tmp, q);

            /* 
             * Question: can we APPLY quality changes w/o writing to a file?
             * if not, can we write out to memory?
             */
            MagickWriteImages(tmp, dst, MagickTrue);
            DestroyMagickWand(tmp);
            tmp = NewMagickWand();
            MagickReadImage(tmp, dst);

            {
                double distortion[CompositeChannels+1];
                (void) GetImageDistortion(GetImageFromMagickWand(tmp), GetImageFromMagickWand(mw),
                                          MeanErrorPerPixelMetric, distortion, exception);
            }
            {
                /* FIXME: why is the crazy divisor necessary? */
                const double error = GetImageFromMagickWand(tmp)->error.mean_error_per_pixel / 380.;
                const double density_ratio = fabs(color_density(tmp) - original_density) / original_density;

                if (error > opt->error_threshold || density_ratio > opt->color_density_ratio)
                {
                    qmin = q;
                } else {
                    qmax = q;
                }
                fprintf(stderr, "%.2f/%.2f@%u ", error, density_ratio, q);
            }
        }
        MagickSetImageCompressionQuality(tmp, (qmax + qmin) / 2);
        putc('\n', stderr);

        exception = DestroyExceptionInfo(exception);
    }

    return tmp;
}

static void filecopy(const char *src, const char *dst, const off_t bytes)
{
    int rd = open(src, O_RDONLY);
    int wr = open(dst, O_WRONLY | O_CREAT, 0644);
#ifdef IMGMIN_USE_MMAP
    /* in-kernel copying, more efficient */
    void *mm = mmap(NULL, (size_t)bytes, PROT_READ | MAP_SHARED, 0, rd, 0);
    if (write(wr, mm, (size_t)bytes) != (ssize_t)bytes)
        perror("write");
#else
    /* userspace copying, less efficient */
    static char buf[32 * 1024];
    while (read(rd, buf, sizeof buf) > 0)
        write(wr, buf, sizeof buf);
#endif
    close(wr);
    close(rd);
}

static void doit(const char *src, const char *dst, const off_t oldsize,
                 const struct imgmin_options *opt)
{
    MagickWand *mw, *tmp;
    MagickBooleanType status;
    double ks = oldsize / 1024.;

    MagickWandGenesis();
    mw = NewMagickWand();

    status = MagickReadImage(mw, src);
    if (status == MagickFalse)
        ThrowWandException(mw);

    fprintf(stderr,
        "Before quality:%lu colors:%lu size:%5.1fkB type:%s ",
        quality(mw),
        (unsigned long)unique_colors(mw),
        ks, type2str(MagickGetImageType(mw)));

    tmp = search_quality(mw, dst, opt);

    /* "Chroma sub-sampling works because human vision is relatively insensitive to
     * small areas of colour. It gives a significant reduction in file sizes, with
     * little loss of perceived quality." [3]
     */
    (void) MagickSetImageProperty(tmp, "jpeg:sampling-factor", "2x2");

    /* strip an image of all profiles and comments */
    (void) MagickStripImage(tmp);

    status = MagickWriteImages(tmp, dst, MagickTrue);
    if (status == MagickFalse)
        ThrowWandException(tmp);

    /* sanity check: fall back to original image if results are larger */
    {
        struct stat st;
        off_t newsize;

        st.st_size = oldsize + 1; /* ensure newsize > oldsize if stat() fails for any reason */
        stat(dst, &st);
        newsize = st.st_size;

        if (newsize > oldsize)
        {
            filecopy(src, dst, oldsize);
            DestroyMagickWand(tmp);
            tmp = CloneMagickWand(mw);
        }

        {
            double kd = newsize / 1024.;
            double ksave = ks - kd;
            double kpct = ksave * 100. / ks;

            fprintf(stderr,
                "After  quality:%lu colors:%lu size:%5.1fkB saved:%5.1fkB (%.1f%%)\n",
                (unsigned long)quality(tmp),
                (unsigned long)unique_colors(tmp),
                kd, ksave, kpct);
        }
    }

    /* tear it down */
    DestroyMagickWand(tmp);
    DestroyMagickWand(mw);
    MagickWandTerminus();
}

static int parse_opts(int argc, char * const argv[], struct imgmin_options *opt)
{
    int i = 1;

    /* default initialization */
    opt->error_threshold     = CMP_THRESHOLD;
    opt->color_density_ratio = COLOR_DENSITY_RATIO;
    opt->min_unique_colors   = MIN_UNIQUE_COLORS;
    opt->quality_out_max     = QUALITY_OUT_MAX;
    opt->quality_out_min     = QUALITY_OUT_MIN;
    opt->quality_in_min      = QUALITY_IN_MIN;
    opt->max_iterations      = MAX_ITERATIONS;

    while (i + 1 < argc)
    {
        /* GNU-style separator to support files with -- prefix
         * example for a file named "--baz": ./foo --bar -- --baz
         */
        if (0 == strcmp("--", argv[i]))
        {
            i += 1;
            break;
        }
        /* if it isn't a cmdline option, we're done */
        if (0 != strncmp("--", argv[i], 2))
            break;
        /* test for each specific flag */
        if (0 == strcmp("--error-threshold", argv[i])) {
            opt->error_threshold = strtod(argv[i+1], NULL);
            i += 2;
        } else if (0 == strcmp("--color-density-ratio", argv[i])) {
            opt->color_density_ratio = strtod(argv[i+1], NULL);
            i += 2;
        } else if (0 == strcmp("--min-unique-colors", argv[i])) {
            opt->min_unique_colors = (unsigned)atoi(argv[i+1]);
            i += 2;
        } else if (0 == strcmp("--quality-out-max", argv[i])) {
            opt->quality_out_max = (unsigned)atoi(argv[i+1]);
            opt->quality_out_max = min(100, opt->quality_out_max);
            i += 2;
        } else if (0 == strcmp("--quality-out-min", argv[i])) {
            opt->quality_out_min = (unsigned)atoi(argv[i+1]);
            opt->quality_out_min = min(100, opt->quality_out_min);
            i += 2;
        } else if (0 == strcmp("--quality-in-min", argv[i])) {
            opt->quality_in_min = (unsigned)atoi(argv[i+1]);
            opt->quality_in_min = min(100, opt->quality_in_min);
            i += 2;
        } else if (0 == strcmp("--max-iterations", argv[i])) {
            opt->max_iterations = (unsigned)atoi(argv[i+1]);
            opt->max_iterations = min(7, opt->max_iterations);
            i += 2;
        } else {
            break;
        }
    }
    return i;
}

int main(int argc, char *argv[])
{

    const char *src;
    const char *dst;
    struct imgmin_options opt;
    int argc_off = parse_opts(argc, argv, &opt);
    if (argc_off + 2 > argc)
    {
        fprintf(stderr, "Usage: %s <image> <dst>\n", argv[0]);
        exit(1);
    }
    src = argv[argc_off];
    dst = argv[argc_off+1];

    {
        struct stat st;
        if (-1 == stat(src, &st))
        {
            perror("does not exist");
            exit(1);
        }
        doit(src, dst, st.st_size, &opt);
    }

    return 0;
}

