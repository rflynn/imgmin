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
#include <wand/MagickWand.h>

#if defined(_POSIX_MAPPED_FILES) && _POSIX_MAPPED_FILES > 0
#define IMGMIN_USE_MMAP
#include <sys/mman.h>
#endif

#define CMP_THRESHOLD              1.00
#define COLOR_DENSITY_RATIO        0.11
#define MIN_UNIQUE_COLORS       4096
#define QUALITY_MAX               95
#define QUALITY_MIN               70
#define QUALITY_MIN_SECONDGUESS   82L
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
    double density = unique_colors(mw) / area;
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

        while (qmax > qmin + 2)
        {
            MagickWand *diff;
            Image *img;
            unsigned q;
            double cmpstddev;
            double distortion;
            double density_ratio;

            q = (qmax + qmin) / 2;
            tmp = CloneMagickWand(mw);
            MagickSetImageCompressionQuality(tmp, q);
#if 1
            MagickWriteImages(tmp, dst, MagickTrue);
            DestroyMagickWand(tmp);
            tmp = NewMagickWand();
            MagickReadImage(tmp, dst);
#else
            img = GetImageFromMagickWand(tmp);
            ModifyImage(&img, exception);
#endif

            /* FIXME: no matter what I do error is always 0 */
            diff = MagickCompareImages(mw, tmp, RootMeanSquaredErrorMetric, &distortion);
            cmpstddev = GetImageFromMagickWand(diff)->error.mean_error_per_pixel * 100.;
            density_ratio = abs(color_density(tmp) - original_density) / original_density;

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
    }

    return tmp;
}

static void filecopy(const char *src, const char *dst, const off_t bytes)
{
    int rd = open(src, O_RDONLY);
    int wr = open(dst, O_WRONLY | O_CREAT, 0644);
#ifdef IMGMIN_USE_MMAP
    void *mm = mmap(NULL, (size_t)bytes, PROT_READ | MAP_SHARED, 0, rd, 0);
    write(wr, mm, bytes);
#else
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

    MagickStripImage(tmp);

    status = MagickWriteImages(tmp, dst, MagickTrue);
    if (status == MagickFalse)
        ThrowWandException(tmp);

    /* TODO: sub-sampling */
    {
        Image *img;
        ImageInfo *ii = AcquireImageInfo();
        ExceptionInfo *exception = AcquireExceptionInfo();
        (void) DeleteImageOption(ii, "filename");
        (void) strcpy(ii->filename, dst);
        img = ReadImage(ii, exception);
        SetImageOption(ii, "sampling-factor", "2x2");
        SetImageOption(ii, "jpeg:optimize-coding", "true");
        WriteImage(ii, img);
        exception = DestroyExceptionInfo(exception);
        ii = DestroyImageInfo(ii);
    }

    /* never produce a larger image; if our results did, fall back to the original */
    {
        struct stat st;
        off_t newsize;
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

