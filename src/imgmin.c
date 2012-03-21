/* ex: set ts=4 et: */
/*
 * Copyright 2011 Ryan Flynn <parseerror@gmail.com>
 *
 * This program is designed to optimize image file size.
 *
 * Specifically, to intelligently perform transformations that reduce file size
 * while preserving visible image quality for casual human use (i.e. web page
 * photo galleries, personal photo collections, etc.)
 *
 * Example use:
 * ./imgmin original.jpg optimized.jpg
 * Read original.jpg and write an optimized version of it to optimized.jpg
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <math.h>
#include <float.h> /* DBL_EPSILON */
#include <wand/MagickWand.h>
#include "imgmin.h"


/* TODO: this is defined in the ImageMagick header magick/magick-type.h */
#ifndef CompositeChannels
#define CompositeChannels 0x2f
#endif

#ifndef MAX_PATH
#define MAX_PATH 256
#endif

/*
 * The most important threshold and the crux of the algorithm;
 * the amount of mean pixel change we're willing to accept.
 * conservative=0.5 safe=0.75 sweetspot=1.00 toohigh=1.25
 */
#define ERROR_THRESHOLD            1.00

/*
 * secondary check that prevents the total number of colors from changing
 * beyond a certain percent. helpful in a small number of cases.
 */
#define COLOR_DENSITY_RATIO        0.11

/*
 * if an image has fewer colors than this, leave it untouched.
 * low-color images like gradients, line-drawings, etc. produce unacceptable
 * artifacts that are currently undetectable
 */
#define MIN_UNIQUE_COLORS       4096

/*
 * default possible output quality range
 * QUALITY_OUT_MAX: maximum=100 conservative=95 aggressive=85
 * QUALITY_OUT_MIN: maximum=100 conservative=70 aggressive=50
 * override: --quality-out-max N
 *           --quality-out-min N
 */
#define QUALITY_OUT_MAX           95
#define QUALITY_OUT_MIN           70

/*
 * if input image quality is already lower than this, assume it has been
 * optimized already and leave it unchanged.
 * this prevents multiple invocations from degrading an image to quality=0
 *
 * override via: --quality-in-min N
 */
#define QUALITY_IN_MIN            82

/*
 * never perform more than this many steps.
 * each step transforms the entire image and whose cost is based on the size
 * and complexity of the image it's working on.
 * trade potential quality improvements for reduced runtime, useful for low
 * latency or very large batch runs
 * override via --max-steps N
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


#ifndef IMGMIN_LIB
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
#endif

/*
 * given a source image, a destination filepath and a set of image metadata thresholds,
 * search for the lowest-quality version of the source image whose properties fall within our
 * thresholds.
 * this will produce an image file that looks the same to the casual observer, but which
 * contains much less information and results in a smaller file.
 * typical savings on unoptimized images vary widely from 10-80%, with 25-50% being most common.
 */
MagickWand * search_quality(MagickWand *mw, const char *dst,
                                   const struct imgmin_options *opt)
{
    MagickWand *tmp = NULL;
    char tmpfile[MAX_PATH] = "/tmp/imgminXXXXXX";
    if (0 == strcmp("-", dst))
    {
        if (-1 == mkstemp(tmpfile))
        {
            perror("mkstemp");
            return CloneMagickWand(mw);
        }
    } else {
        strcpy(tmpfile, dst);
    }

    /*
     * The overwhelming majority of JPEGs are TrueColorType; it is those types, with a low
     * unique color count, that we must avoid.
     */
    if (unique_colors(mw) < opt->min_unique_colors && MagickGetType(mw) != GrayscaleType)
    {
        fprintf(stdout, " Color count is too low, skipping...\n");
        return CloneMagickWand(mw);
    }

    if (quality(mw) < opt->quality_in_min)
    {
        fprintf(stdout, " Quality < %u, won't second-guess...\n", opt->quality_in_min);
        return CloneMagickWand(mw);
    }

    {
        ExceptionInfo *exception = AcquireExceptionInfo();

        const double original_density = color_density(mw);
        unsigned qmax = min(quality(mw), opt->quality_out_max);
        unsigned qmin = opt->quality_out_min;
        unsigned steps = 0;

        /*
         * binary search of quality space for optimally lowest quality that
         * produces an acceptable level of distortion
         */
        while (qmax > qmin + 1 && steps < opt->max_steps)
        {
            double distortion[CompositeChannels+1];
            double error;
            double density_ratio;
            unsigned q;

            steps++;
            q = (qmax + qmin) / 2;

            /* change quality */
            tmp = CloneMagickWand(mw);
            MagickSetImageCompressionQuality(tmp, q);

            /* apply quality change */
            MagickWriteImages(tmp, tmpfile, MagickTrue);
            DestroyMagickWand(tmp);
            tmp = NewMagickWand();
            MagickReadImage(tmp, tmpfile);

            /* quantify distortion produced by quality change
             * NOTE: writes to distortion[], which we don't care about
             * and to image(tmp)->error, which we do care about
             */
            (void) GetImageDistortion(GetImageFromMagickWand(tmp),
                                      GetImageFromMagickWand(mw),
                                      MeanErrorPerPixelMetric,
                                      distortion,
                                      exception);
            /* FIXME: in perlmagick i was getting back a number [0,255.0],
             * here something else is happening... the hardcoded divisor
             * is an imperfect attempt to scale the number back to the
             * perlmagick results. I really need to look into this.
             */
            error = GetImageFromMagickWand(tmp)->error.mean_error_per_pixel / 380.;
            density_ratio = fabs(color_density(tmp) - original_density) / original_density;
        
            /* eliminate half search space based on whether distortion within thresholds */
            if (error > opt->error_threshold || density_ratio > opt->color_density_ratio)
            {
                qmin = q;
            } else {
                qmax = q;
            }
            if (opt->show_progress)
            {
                fprintf(stdout, "%.2f/%.2f@%u ", error, density_ratio, q);
            }
        }
        if (opt->show_progress)
        {
            putc('\n', stdout);
        }

        MagickSetImageCompressionQuality(mw, qmax);

        /* "Chroma sub-sampling works because human vision is relatively insensitive to
         * small areas of colour. It gives a significant reduction in file sizes, with
         * little loss of perceived quality." [3]
         */
        (void) MagickSetImageProperty(mw, "jpeg:sampling-factor", "2x2");

        /* strip an image of all profiles and comments */
        (void) MagickStripImage(mw);

        MagickWriteImages(mw, tmpfile, MagickTrue);
        (void) DestroyMagickWand(tmp);
        tmp = NewMagickWand();
        MagickReadImage(tmp, tmpfile);

        exception = DestroyExceptionInfo(exception);
    }

    return tmp;
}

struct filesize
{
    char tool[64];
    char path[MAX_PATH+1];
    off_t bytes; 
};

static off_t getfilesize(const char *path)
{

    struct stat st;
    if (stat(path, &st) < 0)
    {
        perror("stat");
        st.st_size = (off_t)-1;
    }
    return st.st_size;
}

/*
 * run 'cmd'; if it succeeds then store path and its filesize in fs
 */
static int do_png_cmd2(const char *cmd, char * const argv[], const char *outpath, struct filesize *fs)
{
    int ok = 0;
    pid_t pid;
    pid = fork();
    if (pid == -1) {
        perror("fork");
    } else if (pid == 0) {
        close(2);
        close(1);
        if (execvp(cmd, argv) != 0) {
            perror(cmd);
            exit(1);
        } else {
            exit(0);
        }
    } else {
        int status = 0;
        if (waitpid(pid, &status, 0) < 0) {
            perror("waitpid");
        } else {
            strcpy(fs->tool, cmd);
            strcpy(fs->path, outpath);
            fs->bytes = getfilesize(outpath);
            ok = 1;
        }
    }
    return ok;
}

/*
 * special handling for PNGs
 * there exist many high-quality PNG optimization tools; if they exist then leverage them
 */
static void do_png(MagickWand *mw, const char *src, const char *dst,
                    const struct imgmin_options *opt)
{
    char out[MAX_PATH+1];
    struct filesize files[8];
    int filecnt = 0;
    int srcnamelen = max(0, (int)(strlen(src) - 4)); /* {foo}.png */
    const char *srcext = src + srcnamelen; /* foo{.png} */

    /* our fallback is the unchanged file itself */
    strcpy(files[0].tool, "none");
    strcpy(files[0].path, src);
    files[0].bytes = getfilesize(src);
    filecnt = 1;

    fputc('\n', stderr);

    /* if we find other well-known png optimizers, try them and record their results in files[] */

    if (!system("which pngnq 2>&1 >/dev/null"))
    {
        char * const argv[] = { "pngnq", "-f", src, NULL };
        snprintf(out, sizeof out, "%.*s-nq8%s", srcnamelen, src, srcext);
        filecnt += do_png_cmd2("pngnq", argv, out, files + filecnt);
    }

    if (!system("which pngcrush 2>&1 >/dev/null"))
    {
        char * const argv[] = { "pngcrush", "-force", src, out, NULL };
        snprintf(out, sizeof out, "%.*s-pngcrush%s", srcnamelen, src, srcext);
        filecnt += do_png_cmd2("pngcrush", argv, out, files + filecnt);
    }

    if (!system("which pngquant 2>&1 >/dev/null"))
    {
        char * const argv[] = { "pngquant", "-force", "256", src, NULL };
        snprintf(out, sizeof out, "%.*s-fs8%s", srcnamelen, src, srcext);
        filecnt += do_png_cmd2("pngquant", argv, out, files + filecnt);
    }

    /* find the smallest of all output files[] */
    {
        int i, smalli = 0;
        for (i = 1; i < filecnt; i++)
        {
            fprintf(stderr, " %.1fk@%s", files[i].bytes / 1024.0, files[i].tool);
            if (files[i].bytes < files[smalli].bytes)
                smalli = i;
        }
        files[0] = files[smalli];
    }

    /* cp the smallest resulting file to dst */
    fprintf(stderr, "\nUsing %s %4.1fk...\n", files[0].tool, files[0].bytes / 1024.0);
    {
        char * const argv[] = { "cp", files[0].path, dst, NULL };
        (void) do_png_cmd2("cp", argv, dst, files);
    }

    /* clean up intermediate files (but don't nuke src or dst!) */
    {
        char rmpath[MAX_PATH+1];
        char * const argv[] = { "rm", rmpath, NULL};
        int i;
        for (i = 1; i < filecnt; i++)
        {
            if (strcmp(files[i].path, dst) != 0)
            {
                strcpy(rmpath, files[i].path);
                (void) do_png_cmd2("rm", argv, dst, files);
            }
        }
    }

}

int imgmin_options_init(struct imgmin_options *opt)
{
    /* default initialization */
    opt->error_threshold     = ERROR_THRESHOLD;
    opt->color_density_ratio = COLOR_DENSITY_RATIO;
    opt->min_unique_colors   = MIN_UNIQUE_COLORS;
    opt->quality_out_max     = QUALITY_OUT_MAX;
    opt->quality_out_min     = QUALITY_OUT_MIN;
    opt->quality_in_min      = QUALITY_IN_MIN;
    opt->max_steps           = MAX_STEPS;
    opt->show_progress       = 0;

    return 1;
}

void imgmin_opt_set_error_threshold(
    struct imgmin_options *opt,
    const char *arg)
{
    opt->error_threshold = strtod(arg, NULL);
    /* constrain range */
    if (opt->error_threshold < DBL_EPSILON)
    {
        opt->error_threshold = DBL_EPSILON;
    }
    else if (opt->error_threshold > 255.0)
    {
        opt->error_threshold = 255.0;
    }
}

#ifndef IMGMIN_LIB

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

    fprintf(stdout,
        "Before quality:%lu colors:%lu size:%5.1fkB type:%s format:%s ",
        quality(mw),
        (unsigned long)unique_colors(mw),
        ks, type2str(MagickGetImageType(mw)),
	MagickGetImageFormat(mw));

    if (strcmp("-", src) && !strcmp("PNG", MagickGetImageFormat(mw)))
    {
        do_png(mw, src, dst, opt);
        return;
    } else {
        tmp = search_quality(mw, dst, opt);
    }

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
                if (-1 == fd)
                {
                    perror("open");
                    exit(1);
                }
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

        fprintf(stdout,
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

    imgmin_options_init(opt);

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
            imgmin_opt_set_error_threshold(opt, argv[i+1]);
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

static void help(void)
{
	printf(
    	" --error-threshold 1.0\n"
    	" --color-density-ratio ?\n"
    	" --min-unique-colors N\n"
    	" --quality-out-max N\n"
    	" --quality-out-min N\n"
    	" --quality-in-min N\n"
    	" --max-steps N\n"
	);
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

    setvbuf(stdout, NULL, _IONBF, 0);

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

    opt.show_progress = 1;

    doit(src, dst, oldsize, &opt);

    return 0;
}
#endif

