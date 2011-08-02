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

#ifndef MAX_PATH
#define MAX_PATH 256
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
#define MAX_STEPS                  5

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
             max_steps;
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
    char tmpfile[MAX_PATH] = "/tmp/imgminXXXXXX";
    if (0 == strcmp("-", dst))
        mkstemp(tmpfile);
    else
        strcpy(tmpfile, dst);

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
        unsigned qmin = opt->quality_out_min;
        unsigned steps = 0;

#ifndef CompositeChannels
#define CompositeChannels 0x2f
#endif

        while (qmax > qmin && steps < opt->max_steps)
        {
            unsigned q;

            steps++;
            q = (qmax + qmin) / 2;
            tmp = CloneMagickWand(mw);
            MagickSetImageCompressionQuality(tmp, q);

            /* apply quality setting to tmp */
            MagickWriteImages(tmp, tmpfile, MagickTrue);
            DestroyMagickWand(tmp);
            tmp = NewMagickWand();
            MagickReadImage(tmp, tmpfile);
            /* measure quality effect */
            {
                double distortion[CompositeChannels+1];
                (void) GetImageDistortion(GetImageFromMagickWand(tmp), GetImageFromMagickWand(mw),
                                          MeanErrorPerPixelMetric, distortion, exception);
            }
            /* eliminate half the search space based on whether this mutation is acceptable */
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
        tmp = CloneMagickWand(mw);
        MagickSetImageCompressionQuality(tmp, qmax);
        putc('\n', stderr);

        exception = DestroyExceptionInfo(exception);
    }

    return tmp;
}

static void doit(const char *src, const char *dst, size_t oldsize,
                 const struct imgmin_options *opt)
{
    MagickWand *mw, *tmp;
    MagickBooleanType status;
    double ks;
    size_t newsize = oldsize + 1;

    MagickWandGenesis();
    mw = NewMagickWand();

    /* load image... */
    if (0 == strcmp("-", src))
    {
        /* ...from stdin */
        # define BIGBUF (16 * 1024 * 1024)
        char *blob = malloc(BIGBUF);
        oldsize = read(STDIN_FILENO, blob, BIGBUF);
        if (BIGBUF == oldsize)
        {
            fprintf(stderr, "Image too large for hardcoded imgmin stdin buffer\n");
            exit(1);
        }
        MagickReadImageBlob(mw, blob, oldsize);
        free(blob);
    } else {
        /* ...from disk */
        status = MagickReadImage(mw, src);
        if (status == MagickFalse)
            ThrowWandException(mw);
    }
 
    ks = oldsize / 1024.;

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

    /* output image... */
    {
        unsigned char *blob = MagickGetImageBlob(tmp, &newsize);

        /* if resulting image is larger than original, use original instead */
        if (newsize > oldsize)
        {
            (void) MagickRelinquishMemory(blob);
            blob = MagickGetImageBlob(mw, &newsize);
        }

        {
            int fd;
            if (0 == strcmp("-", dst))
            {
                fd = STDOUT_FILENO;
            } else {
                fd = open(dst, O_WRONLY | O_CREAT, 0644);
            }

            if ((ssize_t)newsize != write(fd, blob, newsize))
            {
                perror("write");
                exit(1);
            }
            (void) MagickRelinquishMemory(blob);
            if (fd != STDOUT_FILENO)
                close(fd);
        }
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
    opt->max_steps           = MAX_STEPS;

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
        } else if (0 == strcmp("--max-steps", argv[i])) {
            opt->max_steps = (unsigned)atoi(argv[i+1]);
            opt->max_steps = min(7, opt->max_steps);
            i += 2;
        } else {
            fprintf(stderr, "Unknown parameter '%s'\n", argv[i]);
            exit(1);
        }
    }
    return i;
}

int main(int argc, char *argv[])
{

    const char *src;
    const char *dst;
    struct imgmin_options opt;
    size_t oldsize = 0;
    int argc_off = parse_opts(argc, argv, &opt);
    if (argc_off + 2 > argc)
    {
        fprintf(stderr, "Usage: %s <image> <dst>\n", argv[0]);
        exit(1);
    }
    src = argv[argc_off];
    dst = argv[argc_off+1];

    if (strlen(src) > MAX_PATH)
    {
        fprintf(stderr, "src path too long: %s", src);
        exit(1);
    }

    if (strlen(dst) > MAX_PATH)
    {
        fprintf(stderr, "dst path too long: %s", dst);
        exit(1);
    }

    if (0 != strcmp("-", src)){
        struct stat st;
        if (-1 == stat(src, &st))
        {
            perror("does not exist");
            exit(1);
        }
        oldsize = (size_t)st.st_size;
    }

    doit(src, dst, oldsize, &opt);

    return 0;
}

