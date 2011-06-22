
Summary

Automated image optimization to save bandwidth, by way of a feedback loop.
Can be incorporated into content workflows and deployment processes.
Generally costs 2-3 seconds of runtime per image, depending on image size.
Uses the excellent and widely available open source library ImageMagick
http://imagemagick.org


Problem

Image can be compressed at different qualities. Often, resampling can remove
data from an image (thus decreasing its size) without noticable visual effects.
Many online images are inadequately compressed; they contain more data than is
necessary. This results in larger images, higher load time latency and higher
bandwidth use.

The solution is to compress images at a higher rate; however every image is
different and visibly degrades at a different resampling quality. This makes
specifying a single numeric quality value problematic; no one setting works
well for all images.

This generally results in sites doing no compression and wasting bandwidth
or relying on designers or frontend people to manually tune each image's quality.
The former degrades your site's performance, and the latter is expensive and
inconsistent, especially when images come from different sources.


Solution

Incorporate a feedback loop that measures how much the image has actually changed,
and let the user specify *that* instead. Currently uses RMSE with a threshold of
1 standard deviation, which generally produces an image file that is 10-50% smaller
and is not visibly degraded.

                     +------- feedback ------+
                     |                       |
                     v                       |
                 [ image ]              [ analyze ]
                     |                       ^
                     |                       |
                     +------ resample -------+

This loop results in a smaller image with a visual quality within an acceptable
visual threshold.


Installation

	Imagemagick dependency:

	Redhat Linux:
		sudo yum install imagemagick

	Ubuntu Linux and Debian Linux:
		sudo apt-get install imagemagick


Example use

$ ./imgmin.sh examples/afghan-girl-before.jpg examples/afghan-girl-after.jpg 
1.06@50 0.64@75 0.91@62 1.00@56 1.02@53 1.01@54 1.01@55 Before:58KB After:34KB Saved:24KB(41%)

$ time ./imgmin.sh examples/lena1-before.jpg examples/lena1-after.jpg 
2.11@50 1.65@75 1.11@87 0.40@93 0.64@90 0.95@88 Before:89KB After:73KB Saved:16KB(17%)

real    0m1.889s
user    0m0.916s
sys     0m0.728s

