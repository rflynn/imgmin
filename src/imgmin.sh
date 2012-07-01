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

function do_png
{
	src=$1
	tmpfile=$2
	pngs=("$src") # always include existing file
	# look for PNG-optimizing programs and run them and save their output filenames
	# in the array $pngs
	if [ $(which pngnq) ]; then
		outfile="${tmpfile::$((${#tmpfile}-4))}-nq8${tmpfile:(-4)}"
		pngnq "$tmpfile"
		pngs[${#pngs[@]}]="$outfile"
	fi
	if [ $(which pngquant) ]; then
		pngquant 256 "$tmpfile"
		outfile="${tmpfile::$((${#tmpfile}-4))}-fs8${tmpfile:$((${#tmpfile}-4))}"
		pngs[${#pngs[@]}]="$outfile"
	fi
	#if [ $(which optipng) ]; then
		#outfile="${tmpfile::$((${#tmpfile}-4))}-optipng${tmpfile:$((${#tmpfile}-4))}"
		#optipng -out "$outfile" "$src"
		#pngs[${#pngs[@]}]="$outfile"
	#fi
	#if [ $(which pngcrush) ]; then
		#outfile="${tmpfile::$((${#tmpfile}-4))}-pngcrush${tmpfile:$((${#tmpfile}-4))}"
		#cp "$src" "$outfile"
		#pngcrush "$outfile"
		#pngs[${#pngs[@]}]="$outfile"
	#fi
	# search $pngs for the smallest file and return it
	smalli=0
	smallbytes=$(stat -c%s "${pngs[0]}")
	curri=1
	while [ $curri -lt ${#pngs[@]} ]
	do
		currbytes=$(stat -c%s "${pngs[$curri]}")
		if [ $currbytes -lt $smallbytes ]; then
			let smalli=curri
		else
			rm -f "${pngs[$curri]}"
		fi
		let curri=curri+1
	done
	echo "${pngs[$smalli]}"
}

function search_quality
{
	src=$1
	tmpfile=$2
	uc=`unique_colors "$src"`
	echo "uc=$uc"
	if [ $((uc < MIN_UNIQUE_COLORS)) ]; then
		#return
		echo "$uc < $MIN_UNIQUE_COLORS"
		if [ ".png" = ${src:(-4)} ]; then
			cp "$src" "$tmpfile"
			use=$(do_png "$src" "$tmpfile")
			echo "use:$use"
			cp "$use" "$tmpfile"
			return
		fi
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

ext=${src:(-3)}
tmpfile="/tmp/imgmin$$.$ext"
search_quality "$src" "$tmpfile"
convert -strip "$tmpfile" "$dst"
kdiff=print_stats
cp "$tmpfile" "$dst"
#rm -f $tmpfile

