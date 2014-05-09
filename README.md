imgmin
======


Get Started!
------------
    $ sudo apt-get install autoconf libmagickwand-dev pngnq pngcrush pngquant
    $ git clone https://github.com/rflynn/imgmin.git
    $ cd imgmin
    $ autoreconf -fi
    $ ./configure
    $ make
    $ sudo make install
    $ imgmin original.jpg optimized.jpg


Summary
-------
Image files constitute a majority of static web traffic.[17]
Unlike text-based web file formats, binary image files do not benefit from
built-in webserver-based HTTP gzip compression.
imgmin offers an automated means for enforcing image quality as a
standalone tool and as a webserver module.
imgmin determines the optimal balance of image quality and filesize, often
greatly reducing image size while retaining quality for casual use, which
translates into more efficient use of storage and network bandwidth, which
saves money and improves user experience.


*****


The Problem
-----------
Websites are composed of several standard components.
Most (HTML, CSS, Javascript, JSON, XML, etc) are text-based.
They can be efficiently compressed for transfer via gzip, supported by all
mainstream webservers and browsers.
But image and video files are binary, non-text files, and generally are not
worth auto-compressing in the webserver.

Most web traffic consists of image file downloads, specifically JPEG images.
JPEG files use so much bandwidth that Google has tried improving them by
introducing an alternative format[16].
JPEG images are not compressed by the webserver because JPEG is a binary format
which does not compress well because it includes its own built-in compression,
and generally it is up to the people creating the images to select an appropriate
compression setting when the file is saved.  

The JPEG quality settings most used by graphics professionals tend to be highly
conservative because Compression and image quality are inversely proportional
and graphics people are interested in utmost visual quality and not in spending
time worrying about network efficiency.

The result of overly conservative JPEG compression and webservers' inability
to compress them any further means that many images on the web are too large.
JPEG's overwhelming popularity as the most common image format means that many
pages contain dozens of JPEG images.

These bloated images take longer to transfer, leading to extended load time,
which does not produce a good viewer experience. People hate to wait.

In order to understand how to optimize JPEGs for size first we must learn more
about the JPEG format.


"Quality" Details
-----------------
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

    100% |#*******
     90% | #      ******* Visual Quality (approximate)
     80% |  #            ********
     70% |   #                   ********    --- noticeably worse at some point ---
     60% |    ##                         *******
     50% |      ###                             ******
     40% |         #####                              ****
     30% |    File Size ######                            ***
     20% |                    ################               *****
     10% |                                    ####################******
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


Even More Detail
----------------
So, why not just force all JPEGs to quality 75 and leave it at that?

Some sites do just that:

    Google Images thumbnails:  74-76
    Facebook full-size images: 85
    Yahoo frontpage JPEGs:     69-91
    Youtube frontpage JPEGs:   70-82
    Wikipedia images:          80
    Windows live background:   82
    Twitter user JPEG images:  30-100, apparently not enforcing quality

This is a fine strategy and is low-risk, straight-forward and inexpensive.

But for optimal results it is not that simple. Compression results rely heavily
on the data being compressed. This means that visual quality is not uniform for
all images at a given quality setting. Imposing a single quality, no matter
what it is, will be too low for some images, resulting in poor visual quality
and will be too high for others, resulting in wasted space.

So we are left with a question:

**What is the optimal quality setting for a given image with regard to filesize
but still remain indistinguishable from the original?**

The widely accepted answer, as formulated by the 'JPEG image compression FAQ':

**This setting will vary from one image to another.**

So, there is no one setting that will save space but still ensure that images
look good, and there's no direct way to predict what the optimal setting is for
a given image.


Looking For Patterns
--------------------
Based on what we know, the easiest way around our limitations would be to
generate multiple versions of an image in a spectrum of qualities and have
a human choose the lowest quality version of the image of acceptable quality.

I proceded in this way for a variety of images, producing an interactive image
gallery. Along with each image version I included several statistical measures
available from the image processing library and a pattern emerged.

Given a high quality original image, apparent visual quality began to diminish
noticably when mean pixel error rate exceeded 1.0.

This metric measures the amount of change, on average, each pixel in the new
image is from the original. Specifically, JPEGs break image data into 8x8 pixel
blocks. The quality setting controls the amount of information available
to encoded quantized color and brightness information about a block. The less
space available to store each block's data the more distorted and pixelated
the image becomes -- you can verify this by inspecting an image saved
at quality 0 -- each 8x8 block of pixels should be assigned a single color.

The change in pixel error rate is not directly related to the quality setting,
again, an image's ultimate fate lies in its data; some images degrade rapidly
within a 1 or 2 quality steps, while others compress with little visible
difference from quality 95 to quality 50.


Automating the Process
----------------------
Given the aforementioned observation of high-quality images looking similar
within a mean pixel error rate of 1.0, the method of determining an optimal
quality setting for any given JPEG is clear: generate versions of an image at
multiple different quality settings, and find the version with the mean pixel
error rate nearest to but not exceeding 1.0.

Using quality bounds of [95, 50] we perform a binary search of the quality
space, converging on the lowest quality setting that produces a mean pixel
error rate of < 1.0.

For general-purpose photographic images with high color counts the above method
yields good results in tests.


Limitations
-----------
One notable exception is in low color JPEG images, such as gradients and low-
contrast patterns used in backgrounds. The results at ~1.0 are often unacceptably
pixelated. Our image-wide statistical measure is not "smart" enough to catch
this, so currently images with < 4096 colors are passed through unchanged.
For reference the "google" logo on google.com contains 6438 colors. In practice
this is not a problem for a typical image-heavy website because there are
relative few layout-specific "background" graphics which can be (and are) handled
separately from the much larger population of "foreground" images.


Implementation
--------------
The implementation for the standalone client and apache module is in C.
The original script is in Perl.
The interactive image gallery in web/ uses PHP.
All use the excellent ImageMagick graphics library.


Performance
-----------
1-3 seconds for a typical image on a typical 2011 machine.
Automatically scales to multiple CPUs via Imagemagick's built-in OpenMP support.


Conclusion
----------
In conclusion I have created an automated method for generating optimally-sized
JPEG images for casual use that can be integrated into existing workflows.
The method is low cost to deploy and run and can yield appreciable and direct
benefits in the form of improving webserver efficiency, reducing website latency,
and most importantly improving overall viewer experience.
This method is generally applicable and can be applied to any collection of or
website containing JPEG images.



References
==========
  1. "JPEG" Wikipedia, The Free Encyclopedia. Wikimedia Foundation, Inc. 3 July 2011. Web. 7 Jul. 2011.
     <http://en.wikipedia.org/wiki/JPEG>
  2. "Joint Photographic Experts Group" Wikipedia, The Free Encyclopedia. Wikimedia Foundation, Inc. 29 June 2011. Web. 7 Jul. 2011.
     <http://en.wikipedia.org/wiki/Joint_Photographic_Experts_Group>
  3. "Information technology – Digital compression and coding of continuous-tone still images – Requirements and guidelines" 1992. Web. 7 Jul. 2011
     <http://www.w3.org/Graphics/JPEG/itu-t81.pdf>
  4. "Independent JPEG Group" 16 Jan. 2011 Web 7 Jul. 2011
      <http://www.ijg.org/>
  5. "JPEG image compression FAQ" Lane, Tom et. al. 28 Mar. 1999 Web. 7 Jul. 2011
     <http://www.faqs.org/faqs/jpeg-faq/part1/preamble.html>
  6. "JPEG Discrete cosine transform". Wikipedia, The Free Encyclopedia. Wikimedia Foundation, Inc. 3 July 2011. Web. 7 Jul. 2011.
     <http://en.wikipedia.org/wiki/JPEG#Discrete_cosine_transform>
  7. "GetImageQuantizeError()" ImageMagick Studio LLC. Revision 4754 [computer program]
     <http://trac.imagemagick.org/browser/ImageMagick/trunk/MagickCore/quantize.c#L2142> (Accessed July 7 2011)
  8. "A Color-based Technique for Measuring Visible Loss for Use in Image Data Communication" Melliyal Annamalai, Aurobindo Sundaram, Bharat Bhargava 1996. Web 10 Jul 2011
     <http://citeseerx.ist.psu.edu/viewdoc/summary?doi=10.1.1.50.8313>
  9. "An Evaluation of Transmitting Compressed Images in a Wide Area Network" Melliyal Annamalai, Bharat Bhargava 1995. Web 10 Jul 2011
     <http://citeseerx.ist.psu.edu/viewdoc/summary?doi=10.1.1.53.7201>
  10. "ImageMagick v6 Examples -- Common Image Formats: JPEG Quality vs File Size" ImageMagick Studio LLC
     <http://www.imagemagick.org/Usage/formats/#jpg_size>
  11. "JPEG Compression, Quality and File Size" ImpulseAdventure.com, Calvin Hass
     <http://www.impulseadventure.com/photo/jpeg-compression.html>
  12. "Designing a JPEG Decoder & Source Code" ImpulseAdventure.com, Calvin Hass
     <http://www.impulseadventure.com/photo/jpeg-decoder.html>
  13. "JPEG Compression" Gernot Hoffmann. 18 Sep 2003. Web. 13 Aug 2011
     <http://www.fho-emden.de/~hoffmann/jpeg131200.pdf>
  14. "Optimization of JPEG (JPG) images: good quality and small size" Alberto Martinez Perez. 16 Sep 2008. Web. 14 Aug 2011
     <http://www.ampsoft.net/webdesign-l/jpeg-compression.html>
  15. "JPEG: Joint Photographic Experts Group"
     <http://www.cs.auckland.ac.nz/compsci708s1c/lectures/jpeg_mpeg/jpeg.html>
  16. "WebP: A new image format for the Web", Google, 2012. Web. 31 Jan 2012.
     <http://code.google.com/speed/webp/>
  17 "New WebP Image Format Could Send JPEG Packing", Rob Spiegel, 10 Oct 2010. Web. 31 Jan 2012
     <http://www.technewsworld.com/story/New-WebP-Image-Format-Could-Send-JPEG-Packing-70945.html>


Technical Notes
===============

License
-------
    This software is licensed under the MIT license.
    See LICENSE-MIT.txt and/or http://www.opensource.org/licenses/mit-license.php


Installation
------------

### Prerequisites

On Ubuntu Linux via `apt-get`:
    
    $ sudo apt-get install imagemagick libgraphicsmagick1-dev libmagickwand-dev perlmagick apache2-prefork-dev

On Redhat Linux via `yum`:
    
    $ sudo yum install Imagemagick ImageMagick-devel Perlmagick apache2-devel

On Unix via source:
    
    $ cd /usr/local/src                                                # source directory of choice
    $ sudo wget -nH -nd ftp://ftp.imagemagick.org/pub/ImageMagick/ImageMagick.tar.gz
    $ sudo gzip -dc ImageMagick-6.7.1-3.tar.gz | sudo tar xvf -        # extract
    $ cd ImageMagick-6.7.1-3                                           # change dir
    $ sudo ./configure                                                 # configure
    $ sudo make -j2                                                    # compile
    $ sudo make install                                                # install

imgmin

    $ git clone git@github.com:rflynn/imgmin.git
    $ cd imgmin
    $ make
    $ sudo make install


Examples
--------


### Generic

    $ imgmin examples/afghan-girl.jpg examples/afghan-girl-after.jpg
    Before quality:85 colors:44958 size: 58.8KB type:TrueColor 0.56/0.03@77 0.67/0.06@73 0.70/0.06@71
    After  quality:70 colors:47836 size: 37.9KB saved:(20.9KB 35.5%)


### Single-core Intel Xeon server

    $ time imgmin examples/lena1.jpg examples/lena1-after.jpg
    Before quality:92 colors:69904 size: 89.7KB type:TrueColor 1.55/0.01@81 1.24/0.12@86 0.81/0.09@89 1.11/0.12@87
    After  quality:88 colors:78327 size: 68.0KB saved:(21.7KB 24.2%)

    real    0m1.467s
    user    0m0.488s
    sys     0m0.941s


### My dual-core laptop

    $ time imgmin examples/lena1.jpg examples/lena1-after.jpg
    Before quality:92 colors:69904 size: 89.7KB type:TrueColor 1.55/0.01@81 1.24/0.12@86 0.81/0.09@89 1.11/0.12@87
    After  quality:88 colors:78327 size: 68.0KB saved:(21.7KB 24.2%)

    real    0m0.931s
    user    0m1.310s
    sys     0m0.090s


