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

static MagickWand * search_quality(MagickWand *mw, const char *dst)
{
    MagickWand *tmp = NULL;

    if (unique_colors(mw) < MIN_UNIQUE_COLORS && MagickGetType(mw) != GrayscaleType)
    {
        fprintf(stderr, " Color count is too low, skipping...\n");
        return CloneMagickWand(mw);
    }

    if (quality(mw) < QUALITY_MIN_SECONDGUESS)
    {
        fprintf(stderr, " Quality < %lu, won't second-guess...\n",
            QUALITY_MIN_SECONDGUESS);
        return CloneMagickWand(mw);
    }

    {
        ExceptionInfo *exception = AcquireExceptionInfo();

        double original_density = color_density(mw);
        unsigned qmax = min(quality(mw), QUALITY_MAX);
        unsigned qmin = max(QUALITY_MAX - (1 << MAX_ITERATIONS), QUALITY_MIN);

#ifndef CompositeChannels
#define CompositeChannels 0x2f
#endif

        while (qmax > qmin + 2)
        {
            unsigned q;
            double cmpstddev;
            double distortion[CompositeChannels+1];
            double density_ratio;

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

            (void) GetImageDistortion(
                GetImageFromMagickWand(tmp),
                GetImageFromMagickWand(mw),
                MeanErrorPerPixelMetric, /* TODO: RootMeanSquaredErrorMetric */
                distortion, exception);
            /* FIXME: why is the crazy divisor necessary? */
            cmpstddev = GetImageFromMagickWand(tmp)->error.mean_error_per_pixel / 380.;

            density_ratio = fabs(color_density(tmp) - original_density) / original_density;

            if (cmpstddev > CMP_THRESHOLD || density_ratio > COLOR_DENSITY_RATIO)
            {
                qmin = q;
            } else {
                qmax = q;
            }
            fprintf(stderr, "%.2f/%.2f@%u ", cmpstddev, density_ratio, q);
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

static void doit(const char *src, const char *dst, const off_t oldsize)
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

    tmp = search_quality(mw, dst);

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
                "After  quality:%lu colors:%lu size:%5.1fkB saved:(%.1fkB %.1f%%)\n",
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

int main(int argc, char *argv[])
{
    struct stat st;
    const char *src, *dst;
    if (argc != 3)
    {
        fprintf(stderr, "Usage: %s <image> <dst>\n", argv[0]);
        exit(1);
    }
    src = argv[1];
    dst = argv[2];
    if (-1 == stat(src, &st))
    {
        perror("does not exist");
        exit(1);
    }
    doit(src, dst, st.st_size);
    return 0;
}

