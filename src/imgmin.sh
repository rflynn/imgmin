#!/bin/bash
#
# Image minimizer
# Iteratively resamples image quality to a certain threshold, reducing image filesize but retaining quality similar to the original image
#
# Example usage:
#	./imgmin.sh foo-before.jpg foo-after.jpg
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

CMP_THRESHOLD=0.90
COLOR_DENSITY_RATIO=0.95
MIN_UNIQUE_COLORS=4096

src=$1
dst=$2

if [ ! -f $src ]; then
	echo "File $src does not exist"
	exit 1
fi

function unique_colors
{
	src=$1
	colors=`convert "$src" -format "%k" info:-`
	return $colors
}

function search_quality
{
	src=$1
	tmpfile=$1
	uc=`unique_colors "$src"`
	echo "uc=$uc"
	if [ $((uc < MIN_UNIQUE_COLORS)) ]; then
		#return
		echo "$uc < $MIN_UNIQUE_COLORS"
	fi
	qmin=0
	qmax=100
	# binary search for lowest quality where compare < $cmpthreshold
	while [ $qmax -gt $((qmin+1)) ]
	do
		q=$(((qmax+qmin)/2))
		convert -quality $q $src $tmpfile
		cmppct=`compare -metric RMSE $src $tmpfile /dev/null 2>&1 | cut -d '(' -f2 | cut -d ')' -f1`
		cmppct=`echo $cmppct*100 | bc`
		if [ `echo "$cmppct > $cmpthreshold" | bc` -eq 1 ]; then
			qmin=$q
		else
			qmax=$q
		fi
		printf "%.2f@%u " $cmppct $q
	done
}

function print_stats
{
	k0=$((`stat -c %s $src` / 1024))
	k1=$((`stat -c %s $tmpfile` / 1024))
	kdiff=$((($k0-$k1) * 100 / $k0))
	if [ $kdiff -eq 0 ]; then
		k1=$k0
		kdiff=0
	fi
	echo "Before:${k0}KB After:${k1}KB Saved:$((k0-k1))KB($kdiff%)"
	return $kdiff
}

color_cnt=`unique_colors $src`
pixel_count=$((`convert "$src" -format "%w*%h" info:-`))
original_density=$((color_cnt / pixel_count))

tmpfile=/tmp/imgmin$$.jpg
search_quality $src $tmpfile
convert -strip $tmpfile $dst
kdiff=print_stats
cp $tmpfile $dst
#rm -f $tmpfile

