#!/usr/bin/perl -w
#
# Author: Ryan Flynn <parseerror+imgmin@gmail.com>
# imgmin via Perl/PerlMagick
# imgmin: https://github.com/rflynn/imgmin
# PerlMagick: http://www.imagemagick.org/script/perl-magick.php
#

# TODO: consider incorporating: http://en.wikipedia.org/wiki/Color_vision#Physiology_of_color_perception

use strict;
use Image::Magick;
use File::Copy qw(copy);
use List::Util qw(max);

$|++;

use constant CMP_THRESHOLD		=>    0.90;
use constant COLOR_DENSITY_RATIO	=>    0.95; 
use constant MIN_UNIQUE_COLORS		=> 4096; 

if ($#ARGV < 1)
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

my $tmp = search_quality($img);
$tmp->Strip(); # strip an image of all profiles and comments
$tmp->Write($dst);

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
	my ($img) = @_;
	my $tmp = $img->Clone();

	if (unique_colors($tmp) < MIN_UNIQUE_COLORS)
	{
		return $tmp;
	}

	my $original_density = color_density($img);
	my $qmin =   0;
	my $qmax = 100;

	while ($qmax > max(2, $qmin+1))
	{
		my ($q, $diff, $cmpstddev);
		$q = ($qmax + $qmin) / 2;
		$tmp = $img->Clone();
		$tmp->Set(quality => $q);
		$tmp->Write($dst);
		undef $tmp;
		$tmp = Image::Magick->new();
		$tmp->Read($dst);
		$diff = $img->Compare(  image => $tmp,
					metric => 'rmse');
		$cmpstddev = $diff->Get('error') * 100;
		my $density_ratio = color_density($tmp) / $original_density;
		if ($cmpstddev > CMP_THRESHOLD || $density_ratio < COLOR_DENSITY_RATIO)
		{
			$qmin = $q;
		} else {
			$qmax = $q;
		}
		printf " %.2f/%.2f@%u", $cmpstddev, $density_ratio, $q;
	}
	print "\n";
	return $tmp;
}

sub print_stats
{
	my $k0 = (-s $src) / 1024.;
	my $k1 = (-s $dst) / 1024.;
	my $ksave = $k0 - $k1;
	my $kpct = $ksave * 100 / $k0;
	printf "Before:%.1fKB After:%.1fKB Saved:%.1fKB(%.1f%%)\n",
		$k0, $k1, $ksave, $kpct;
}

