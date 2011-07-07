
The Problem

Websites are composed of several standard components.
	HTML describes overall page content and organization
	CSS describes specific page layout and style
	Javascript allows interactive client-side programming
	XML is used for data exchange such as RSS feeds
	JPEG is a file format for photo-realistic images

All but one of these component types are text-based. Text files can be
automatically compressed by a webserver using gzip, which is supported by all
major browsers.

Most web traffic consists of image file downloads, specifically JPEG images.
JPEG images are not compressed by the webserver because JPEG is a binary format
which does not compress well because it includes its own built-in compression,
and generally it is up to the people creating the images to select an appropriate
compression setting.  Compression and image quality are inversely proportional.
The JPEG quality settings most used by graphics professional tend to be highly
conservative, for several reasons.

Firstly, JPEG is a "lossy" file format; once the quality/compression level has
been applied to an image and saved, information can be lost. Visible quality
errors (known as "artifacts") accumulate with repeated edits, so it is in
graphical best interests to choose a very high quality setting in case a
future edit is necessary.

Secondly, graphics people tend to think of file sizes differently than backend
web and network engineers. They are used to dealing with bitmapped files
in the 100MBs range; a 100K JPEG file, at 1/10 of 1MB, does not seem large in
comparison.

Lastly, many graphics people tend to value their work and are generally
hesitant to introduce any artifact, no matter how insignificant.

The result of overly conservative JPEG compression and webservers' inability
to compress them any further means that many images on the web are too large.
JPEG's overwhelming popularity as the most common image format means that many
pages contain dozens of JPEG images.

These bloated images take longer to transfer, leading to extended load time,
which does not produce a good viewer experience. People hate to wait.


"Quality" Details

JPEG images contain a single setting usually referred to as "Quality",
and it is usually expressed as a number from 1-100, 100 being the highest.
This knob controls how aggressive the editing program is when saving the
file. A lower quality setting means more aggressive compression, which
generally leads to lower image quality. Many graphics people are hesitant to
reduce this number below 90-95.

But how exactly does "quality" affect the image visibly? Does the same
image at quality 50 look "half as good" than quality 100? What does half 
as good even mean, anyway? Can people tell the difference between an image
saved at quality 90 and quality 89? And how much smaller is an image saved
at a given quality? Is the same image at quality 50 half as large as at 100?

Here is a chart of the approximate relationship between the visual effect of
"quality" and the size of the resulting file.

 100% |##*********                                                   
  90% |  ###      ******* Visual Quality                         
  80% |     ####         ********                                    
  70% |         ####             ********                            
  60% |             ####                 *******                     
  50% |                 ##### File Size         *****                
  40% |                      ######                  ****            
  30% |                            ########              ****        
  20% |                                    ###########       ****    
  10% |                                               ###########****
   0% +---------------------------------------------------------------
     100    90    80    70     60    50    40     30     20    10    0  

The precise numbers vary for each image, but the convex shape of the "Visual
Quality" curve and the concave "File Size" curve hold for each image. This is
the key to reducing file size.

For an average JPEG there is a very minor, mostly insignificant change in
*apparent* quality from 100-75, but a significant filesize difference for
each step down. This means that many images look good to the casual viewer
at quality 75, but are half as large than they would be at quality 95. As
quality drops below 75 there are larger apparent visual changes and reduced
savings in filesize.

The ability to reduce an images' size by 50% means that for many images the
potential exists to transmit them to viewers *twice as fast*, resulting in
significant reduction in latency and overall load time, leading to a better
viewer experience.


Even More Detail

So, why not just force all JPEGs to quality 75 and leave it at that?

Unfortunately it is not that simple. Compression is a tricky business, and
its results rely heavily on the specific contents of the image. This means
that visual quality is not uniform for all images at a given quality setting.
This means that imposing a single quality, no matter what it is, will be
too low for some images, resulting in poor visual quality and will be
too high for others, resulting in wasted space.

So we are left with a question:

  What is the optimal quality setting for a given image that takes up the
  least space but still looks "good"?

The widely accepted answer, as formulated by the 'JPEG image compression FAQ':

   This setting will vary from one image to another.

So, there is no one setting that will save space but still ensure that images
look good, and there's no direct way to predict what the optimal setting is for
a given image.


Looking For Patterns

Based on what we know, the easiest way around our limitations would be to
generate multiple versions of an image in a spectrum of qualities and have
a human choose the lowest quality version of the image of acceptable quality.

I proceded in this way for a variety of images, producing an interactive image
gallery. Along with each image version I included several statistical measures
available from the image processing library, and a pattern emerged.

Given a high quality original image, apparent visual quality began to diminish
noticably when mean pixel error rate exceeded 1 standard deviation.

This metric measures the amount of change, on average, each pixel in the new
image is from the original. Specifically, JPEGs break image data into 8x8 pixel
blocks. The quality setting controls the amount of information available
to encoded quantized color and brightness information about block. The less
space available to store each block's data the more highly distorted and
pixelated the image becomes.

The change in pixel error rate is not directly related to the quality setting,
again, an image's ultimate fate lies in its data; some images degrade rapidly
within a 1 or 2 quality steps, while others compress with little visible
difference from qualty 95 to quality 50.


Automating the Process

Given the aforementioned observation of high-quality images looking similar
within a mean pixel error rate of 1σ, the method of determining an optimal
quality setting for any given JPEG is clear: generate versions of an image at
multiple different quality settings, and find the version with the mean pixel
error rate nearest to but not exceeding 1σ.

Using quality bounds of [95, 50] we perform a binary search of the quality
space, converging on the lowest quality setting that produces a mean pixel
error rate of < 1σ.

For general-purpose photographic images with high color counts the above method
yields good results in tests.


Limitations

One notable exception is in low color JPEG images, such as gradients and low-
contrast patterns used in backgrounds. The results at ~1σ are often unacceptably
pixelated. Our image-wide statistical measure is not "smart" enough to catch
this, so currently images with < 4096 colors are passed through unchanged.
For reference the "google" logo on google.com contains 6438 colors. In practice
this is not a problem for a typical image-heavy website because there are
relative few layout-specific "background" graphics which can be (and are) handled
separately from the much larger population of "foreground" images.


Implementation

perl, imagemagick  perlmagick, etc.
Runtime: 1-3 seconds per image; automatically scales to multiple CPUs via
Imagemagick's built-in OpenMP support.


Conclusion

In conclusion I have created an automated method for determining optimal JPEG
compression settings that can be integrated into existing workflows. The method
is low cost to deploy and run and can yield appreciable and direct benefits in the form
of improving webserver efficiency, reducing website latency, and most
importantly improving overall viewer experience. This method is generally
applicable and can be applied to any website containing JPEG images.



Technical Notes
----------------

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
Before quality:85 colors:44958 size: 58.8KB type:TrueColor 0.56/0.03@77 0.67/0.06@73 0.70/0.06@71
After  quality:70 colors:47836 size: 37.9KB saved:(20.9KB 35.5%)

# on a single-core Intel Xeon server 

$ time ./imgmin.pl examples/lena1.jpg examples/lena1-after.jpg 
Before quality:92 colors:69904 size: 89.7KB type:TrueColor 1.55/0.01@81 1.24/0.12@86 0.81/0.09@89 1.11/0.12@87 
After  quality:88 colors:78327 size: 68.0KB saved:(21.7KB 24.2%)

real    0m1.467s
user    0m0.488s
sys     0m0.941s

# on my dual-core laptop

$ time ./imgmin.pl examples/lena1.jpg examples/lena1-after.jpg
Before quality:92 colors:69904 size: 89.7KB type:TrueColor 1.55/0.01@81 1.24/0.12@86 0.81/0.09@89 1.11/0.12@87
After  quality:88 colors:78327 size: 68.0KB saved:(21.7KB 24.2%)

real    0m0.931s
user    0m1.310s
sys     0m0.090s


