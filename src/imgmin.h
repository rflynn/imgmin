/* ex: set ts=4 et: */

#ifndef IMGMIN_H
#define IMGMIN_H

/* ImageMagick */
#include <wand/MagickWand.h>

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

int imgmin_options_init(struct imgmin_options *opt);
void imgmin_opt_set_error_threshold(struct imgmin_options *opt, const char *arg);

MagickWand * search_quality(MagickWand *mw,
                            const char *dst,
                            const struct imgmin_options *opt);

#endif

