#!/usr/bin/perl -w
#
# Author: Ryan Flynn <parseerror+imgmin@gmail.com>
# imgmin via Perl/PerlMagick
# imgmin: https://github.com/rflynn/imgmin
# PerlMagick: http://www.imagemagick.org/script/perl-magick.php
#

#
# typically images contain more information than humans need to view it.
# the goal of this program is to reduce image file size by reducing quality
# while not visibly affecting image quality for casual use.
#

# TODO: consider incorporating: http://en.wikipedia.org/wiki/Color_vision#Physiology_of_color_perception

use strict;
use Image::Magick 6.7.0; # does not work with perlmagick 6.5.1, does with 6.7.0.8, not sure about in between
use File::Copy qw(copy);
use List::Util qw(max min);

$|++;

use constant CMP_THRESHOLD		=>    1.00; # do not allow average pixel error to exceed this number of standard deviations
use constant COLOR_DENSITY_RATIO	=>    0.10; # never reduce color count to less than this amount of the original
use constant MIN_UNIQUE_COLORS		=> 4096;    # never compress an image with fewer colors; they pixelate
use constant QUALITY_MAX  		=>  100;    # maximum possible JPEG quality
use constant QUALITY_MIN  		=>   50;    # minimum quality bound, never go below
use constant MAX_ITERATIONS		=>    5;    # maximum number of steps

printf "Image::Magick %s\n", $Image::Magick::VERSION;

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

my $img = Image::Magick->new();
$img->Read($src);

printf "Before quality:%u colors:%u size:%.1fKB\n",
	quality($img), unique_colors($img), (-s $src) / 1024.;
my $QUALITY_MAX = min(quality($img), QUALITY_MAX);
my $QUALITY_MIN = max($QUALITY_MAX - MAX_ITERATIONS ** 2, QUALITY_MIN);

my $tmp = search_quality($img, $dst);
$tmp->Strip(); # strip an image of all profiles and comments
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
	my $tmp = $img->Clone();

	if (unique_colors($tmp) < MIN_UNIQUE_COLORS)
	{
		return $tmp;
	}

	my $original_density = color_density($img);
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
