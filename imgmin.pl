#!/usr/bin/perl -w
#
# imgmin via Perl/PerlMagick
# imgmin: https://github.com/rflynn/imgmin
# PerlMagick: http://www.imagemagick.org/script/perl-magick.php
#

use strict;
use Image::Magick;
use File::Copy qw(copy);

$|++;

=head1
Color density refers to the ratio of total unique colors in an image
and its total size (height * width in pixels).

	color_density = width * height / unique_colors

Generally the types of images that compress best have large amounts of
unique colors which can be reduced without noticably affecting the image
visibly.
Some types of images though, like gradients (fades) and line-drawings
have a small amount of total colors, so reducing the quality at all results
in awful pixelation.
In my experience a photo-type image has a color_density in the range of [1,13].
Gradients otoh have a color_density > 1000.
This threshold is used to avoid even attempting to optimize gradients and other
images known to do poorly.
=cut
use constant COLOR_DENSITY_THRESHOLD => 20;

if ($#ARGV < 1)
{
	print "Usage: $0 <image> <dst> [quality]\n";
	exit 1;
}

my ($src, $dst, $cmpthreshold) = @ARGV;
$cmpthreshold ||= 1.0;

if (! -f $src)
{
	print "File $src does not exist\n";
	exit 1;
}

my $img = Image::Magick->new();
$img->Read($src);

# calculate inverse color density
my $width = $img->Get('%w');
my $height = $img->Get('%h');
my $unique_colors = $img->Get('%k');
my $density = $width * $height / $unique_colors;
if ($density < COLOR_DENSITY_THRESHOLD)
{
	my $tmp = search_quality($img, $cmpthreshold);
	$tmp->Strip(); # strip an image of all profiles and comments
	$tmp->Write($dst);
}
else
{
	copy($src, $dst) or die $!;
}

print_stats();

exit;

=head1
search for the smallest (lowest quality) image whose
mean pixel error is within cmpthreshold
=cut
sub search_quality
{
	my ($img, $cmpthreshold) = @_;

	my $tmp;
	my $qmin =   0;
	my $qmax = 100;

	while ($qmax > $qmin+1)
	{
		my ($q, $diff, $cmppct);
		$q = ($qmax + $qmin) / 2;
		$tmp = $img->Clone();
		$tmp->Set(quality => $q);
		$tmp->Write($dst);
		undef $tmp;
		$tmp = Image::Magick->new();
		$tmp->Read($dst);
		$diff = $img->Compare(  image => $tmp,
					metric => 'rmse');
		$cmppct = $diff->Get('error') * 100;
		if ($cmppct > $cmpthreshold) {
			$qmin = $q;
		} else {
			$qmax = $q;
		}
		printf " %.2f@%u", $cmppct, $q;
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

