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
use Image::Magick;
use File::Copy qw(copy);
use List::Util qw(max min);

$|++;

use constant CMP_THRESHOLD		=>    1.00;
use constant COLOR_DENSITY_RATIO	=>    0.90; 
use constant MIN_UNIQUE_COLORS		=> 4096; 
use constant QUALITY_MAX  		=>  100; 
use constant QUALITY_MIN  		=>   50; 
use constant MAX_ITERATIONS		=>    6;

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

my $QUALITY_MAX = min($img->Get('quality'), QUALITY_MAX);
printf "@%s", $QUALITY_MAX;
my $QUALITY_MIN = max($QUALITY_MAX - MAX_ITERATIONS ** 2, QUALITY_MIN);

my $tmp = search_quality($img, $dst);
$tmp->Strip(); # strip an image of all profiles and comments
$tmp->Write($dst);

# never produce a larger image; if our results did, fall back to the original
if (-s $dst > -s $src)
{
	copy($src, $dst) or die $!;
}
print_stats();

exit;

sub unique_colors
{
	my ($img) = @_;
	my $unique_colors = $img->Get('%k');
	return $unique_colors;
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
		my $density_ratio = color_density($tmp) / $original_density;
		# divide search space in half; which half depending on whether this step passed or not
		if ($cmpstddev > CMP_THRESHOLD || $density_ratio < COLOR_DENSITY_RATIO)
		{
			$qmin = $q;
		} else {
			$qmax = $q;
		}
		printf " %.2f/%.2f@%u", $cmpstddev, $density_ratio, $q;
	}
	return $tmp;
}

sub print_stats
{
	my $k0 = (-s $src) / 1024.;
	my $k1 = (-s $dst) / 1024.;
	my $ksave = $k0 - $k1;
	my $kpct = $ksave * 100 / $k0;
	printf "\nBefore:%.1fKB After:%.1fKB Saved:%.1fKB(%.1f%%)\n",
		$k0, $k1, $ksave, $kpct;
}

