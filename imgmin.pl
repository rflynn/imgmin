#!/usr/bin/perl -w
#
# Author: Ryan Flynn <parseerror+imgmin@gmail.com>
# imgmin via Perl/PerlMagick
# imgmin: https://github.com/rflynn/imgmin
#
# typically images contain more information than humans need to view it.
# the goal of this program is to reduce image file size by reducing quality
# while not visibly affecting image quality for casual use.
#
# References:
#  1. "Speed Matters" http://googleresearch.blogspot.com/2009/06/speed-matters.html
#  2. "JPEG image compression FAQ" http://www.faqs.org/faqs/jpeg-faq/part1/
#  3. "Chroma Subsampling" http://en.wikipedia.org/wiki/Chroma_subsampling
#  4. "Chroma sub-sampling" http://photo.net/learn/jpeg/#chrom
#  5. "Physiology of Color Perception" http://en.wikipedia.org/wiki/Color_vision#Physiology_of_color_perception
#  6. "PerlMagick" http://www.imagemagick.org/script/perl-magick.php

use strict;
use Image::Magick 6.6.2; # does not work with perlmagick 6.5.1, does with 6.6.2+, not sure about in between
use File::Copy qw(copy);
use List::Util qw(max min);

$|++;

# do not allow average pixel error to exceed this number of standard deviations
# this is our best indicator of overall image change; it isn't perfect though
# because in many images certain areas are more important than others
use constant CMP_THRESHOLD		=>    1.00;

# never modify color count more than this amount; it indicates too much change
use constant COLOR_DENSITY_RATIO	=>    0.11;

# "JPEG is designed for compressing either full-color or gray-scale images
# of natural, real-world scenes.  It works well on photographs, naturalistic
# artwork, and similar material."[2]
# JPEGs of full-color photographs will tend to have tens of thousands of colors,
# and it is hard for a human to notice the difference when a few thousand have
# changed; but low-color images, specifically smooth gradients, images with
# text, background images with light texture, etc. are much more susceptible
# to pixelation and degradation; use this hueristic to avoid them.
use constant MIN_UNIQUE_COLORS		=> 4096;

# "Except for experimental purposes, never go above about Q 95; using Q 100
# will produce a file two or three times as large as Q 95, but of hardly any
# better quality."[2]
use constant QUALITY_MAX  		=>   95;

# minimum quality to consider for an image
# "For good-quality, full-color source images, the default IJG quality setting
# (Q 75) is very often the best choice."[2]
# "If the image was less than perfect quality to begin with, you might be able
# to drop down to Q 50 without objectionable degradation."[2]
# NOTE: 70 is a conservative setting, consider lowering it
use constant QUALITY_MIN  		=>   70;

# if the quality is already less than this, assume that a human has manually
# optimized the image and do not try and second-guess them.
use constant QUALITY_MIN_SECONDGUESS	=>   82;

# Ensure bounded worst-case performance in terms of number of intermediate
# images
use constant MAX_ITERATIONS		=>    5;    # maximum number of steps

#printf "Image::Magick %s\n", $Image::Magick::VERSION;

if ($#ARGV != 1)
{
	print "Usage: $0 <image> <dst>\n";
	exit 1;
}

my ($src, $dst) = @ARGV;

if (! -f $src)
{
	print "File $src does not exist\n";
	exit 1;
}

# image file verification
if ($src !~ /\.jpe?g$/i)
{
	if ($src =~ /\.(?:gif|png)$/i)
	{
		printf "'%s' is not a JPEG, pass through...\n", $src;
		copy($src, $dst) or die $!;
		exit 0;
	} else {
		printf "'%s' is not an image file.\n", $src;
		exit 1;
	}
}

my $img = Image::Magick->new();
$img->Read($src);

printf "Before quality:%u colors:%u size:%.1fKB\n",
	quality($img), unique_colors($img), (-s $src) / 1024.;
my $QUALITY_MAX = min(quality($img), QUALITY_MAX);
my $QUALITY_MIN = max($QUALITY_MAX - MAX_ITERATIONS ** 2, QUALITY_MIN);

my $tmp = search_quality($img, $dst);

# "Chroma sub-sampling works because human vision is relatively insensitive to
# small areas of colour. It gives a significant reduction in file sizes, with
# little loss of perceived quality." [3]
$tmp->Set('sampling-factor' => '2x2');

# strip an image of all profiles and comments
$tmp->Strip();

$tmp->Write($dst);

# never produce a larger image; if our results did, fall back to the original
if (-s $dst > -s $src)
{
	copy($src, $dst) or die $!;
	$tmp = $img->Clone();
}

my $ks = (-s $src) / 1024.;
my $kd = (-s $dst) / 1024.;
my $ksave = $ks - $kd;
my $kpct = $ksave * 100 / $ks;

printf "After quality:%u colors:%u size:%.1fKB saved:(%.1fKB %.1f%%)\n",
	quality($tmp), unique_colors($tmp), $kd, $ksave, $kpct;

exit;

sub quality
{
	return $_[0]->Get('%Q')
}

sub unique_colors
{
	return $_[0]->Get('%k')
}

sub color_density
{
	my ($img) = @_;
	my $width = $img->Get('%w');
	my $height = $img->Get('%h');
	my $density = unique_colors($img) / ($width * $height);
	return $density;
}

=head1
search for the smallest (lowest quality) image that retains
the visual quality of the original
=cut
sub search_quality
{
	my ($img, $dst) = @_;

	if (unique_colors($img) < MIN_UNIQUE_COLORS)
	{
		printf "Color count is too low, skipping...\n",
			MIN_UNIQUE_COLORS;
		return $img;
	}

	if (quality($img) < QUALITY_MIN_SECONDGUESS)
	{
		printf "Image quality is < %u, won't second-guess...\n",
			QUALITY_MIN_SECONDGUESS;
		return $img;
	}

	my $original_density = color_density($img);
	my $tmp = $img->Clone();
	my $qmin = $QUALITY_MIN;
	my $qmax = $QUALITY_MAX;

	# binary search for lowest quality within given thresholds
	while ($qmax > $qmin + 1)
	{
		my ($q, $diff, $cmpstddev);
		$q = ($qmax + $qmin) / 2;
		$tmp = $img->Clone();
		$tmp->Set(quality => $q);
		# write image out and read it back in for quality change to take effect
		$tmp->Write($dst);
		undef $tmp;
		$tmp = Image::Magick->new();
		$tmp->Read($dst);
		# calculate difference between 'tmp' image and original
		$diff = $img->Compare(  image => $tmp,
					metric => 'rmse');
		$cmpstddev = $diff->Get('error') * 100;
		my $density_ratio = abs(color_density($tmp) - $original_density) / $original_density;
		# divide search space in half; which half depending on whether this step passed or not
		if ($cmpstddev > CMP_THRESHOLD || $density_ratio > COLOR_DENSITY_RATIO)
		{
			$qmin = $q;
		} else {
			$qmax = $q;
		}
		printf " %.2f/%.2f@%u", $cmpstddev, $density_ratio, $q;
	}
	print  "\n";
	return $tmp;
}
