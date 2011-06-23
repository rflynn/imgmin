#!/bin/sh

# check the color density of image files
# photo-type images score ~1-20; gradient images score much higher, e.g. ~2500
# we can use this to detect which images to skip

if [ $# -lt 1 ]; then
	echo "Usage: $0 <image or folder>"
	exit 1
fi

if [ ! -e $1 ]; then
	echo "$1 does not exist"
	exit 1
fi

for f in $@;
do
	cnt=`convert $f -format "%h*%w/%k" info:-`
	density=`echo $cnt | bc -l`
	printf "%-40s %-20s %7.2f\n" $f $cnt $density
done

