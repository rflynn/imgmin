
Summary

Automated image optimization to save bandwidth, by way of a feedback loop.
Can be incorporated into content workflows and deployment processes.
Generally costs 1-3 seconds of runtime per image, depending on image size.
Uses the excellent and widely available open source library ImageMagick
http://imagemagick.org


Problem

Images can be compressed at different qualities. Often, resampling can remove
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

		and then the language-specific bindings if you want them:

		sudo apt-get install perlmagick
		sudo apt-get install python-pythonmagick


Example use

$ ./imgmin.pl examples/afghan-girl.jpg examples/afghan-girl-after.jpg
Before quality:85 colors:44958 size:58.8KB
 0.56/0.03@77 0.67/0.06@73 0.70/0.06@71
After quality:71 colors:47836 size:38.4KB saved:(20.4KB 34.7%)

$ time ./imgmin.pl examples/lena1.jpg examples/lena1-after2.jpg
Before quality:92 colors:69904 size:89.7KB
 1.55/0.01@81 1.24/0.12@86 0.81/0.09@89 1.11/0.12@87 0.95/0.10@88
After quality:88 colors:77190 size:68.4KB saved:(21.2KB 23.7%)

real    0m1.093s
user    0m1.590s
sys     0m0.090s

