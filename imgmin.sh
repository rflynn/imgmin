#!/bin/sh
#
# Image minimizer
# Iteratively resamples image quality to a certain threshold, reducing image filesize but retaining quality similar to the original image
#
# Example usage:
#	./imgmin.sh foo.jpg foo-min1.jpg	# default 1 standard deviation
#	./imgmin.sh foo.jpg foo-min2.jpg 2	# quality within 2 standard deviation
#
# Author: Ryan Flynn <parseerror+imgmin@gmail.com>
#
# Requires:
#  Imagemagick tools 'convert' and 'compare' http://www.imagemagick.org/
#
# References:
#   1. "Optimization of JPEG (JPG) images: good quality and small size", Retrieved May 23 2011, http://www.ampsoft.net/webdesign-l/jpeg-compression.html
#   2. "Convert, Edit, Or Compose Images From The Command-Line" In ImageMagick, Retrieved May 24 2011, http://www.imagemagick.org/script/command-line-tools.php
#   3. "Bash Floating Point Comparison", http://unstableme.blogspot.com/2008/06/bash-float-comparison-bc.html

if [ -z $1 ] || [ -z $2 ]; then
	echo "Usage $0 <image> <dst> [quality]"
	exit 1
fi

# needed for webservers
PATH=$PATH:/usr/local/bin:/usr/bin # FIXME: be smarter

src=$1
dst=$2
cmpthreshold=${3:-1}

if [ ! -f $src ]; then
	echo "File $src does not exist"
	exit 1
fi

# if file isn't color-dense enough it'll get badly pixelated
# most of the photos i'm working on now have a value of 1-10
INV_COLOR_DENSITY_THRESHOLD=50
inv_color_density=`convert "$src" -format "%h*%w/%k" info:- | bc -l`
if [ `echo "$inv_color_density > $INV_COLOR_DENSITY_THRESHOLD" | bc` -eq 1 ]; then
	echo "Color density too low, using original."
	cp $src $dst
	exit 0
fi

ext=${src##*.}
tmpfile=/tmp/imgmin$$.jpg


qmin=0
qmax=100
# binary search for lowest quality where compare < $cmpthreshold
while [ $qmax -gt $((qmin+1)) ]
do
	q=$(((qmax+qmin)/2))
	convert -quality $q -interlace Line -strip $src $tmpfile
	cmppct=`compare -metric RMSE $src $tmpfile /dev/null 2>&1 | cut -d '(' -f2 | cut -d ')' -f1`
	cmppct=`echo $cmppct*100 | bc`
	if [ `echo "$cmppct > $cmpthreshold" | bc` -eq 1 ]; then
		qmin=$q
	else
		qmax=$q
	fi
	printf "%.2f@%u " $cmppct $q
done

k0=$((`stat -c %s $src` / 1024))
k1=$((`stat -c %s $tmpfile` / 1024))
kdiff=$((($k0-$k1) * 100 / $k0))

if [ $kdiff -gt 0 ]; then
	mv $tmpfile $dst
else
	cp $src $dst
	k1=$k0
	kdiff=0
fi
echo "Before:${k0}KB After:${k1}KB Saved:$((k0-k1))KB($kdiff%)"

