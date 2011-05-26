#!/usr/bin/env python

"""
Image minimizer.
Reduces image size while not exceeding quality threshold.
A reduction in quality of 1-3% is usually unnoticeable and typically yields a
reduction in size of 10-60%.

Author: Ryan Flynn <parseerror+imgmin@gmail.com>

Requires: Imagemagick tools 'convert' and 'compare'

Limitations:
	Currently only useful on JPEG
"""

import sys, os, re, shutil, math, tempfile
import pipes
from subprocess import Popen,PIPE
from optparse import OptionParser

def convert(quality, src, dst):
	os.system('convert -quality %u -interlace Line -strip %s %s' % (
		quality, pipes.quote(src), pipes.quote(dst)))

def compare(src, dst):
	cmd = 'compare -metric RMSE %s %s /dev/null 2>&1' % (pipes.quote(src), pipes.quote(dst))
	pipe = Popen(cmd, shell=True, stdout=PIPE).stdout
	x = pipe.readline()
	pipe.close()
	return float(re.search('\((.*?)\)', x).groups(0)[0]) * 100

ComparePctMin = 0.1
ComparePctMax = 10
ComparePctThreshold = 2.0

parser = OptionParser()
parser.add_option("-m", "--max",
	dest="max", type="float", default=ComparePctThreshold,
	help="maximum pct difference [%.1f-%.0f] default:%.1f" % (ComparePctMin, ComparePctMax, ComparePctThreshold))
parser.add_option("-q", "--quiet", action="store_false", dest="verbose", default=True, help="STFU")
(Opts, Args) = parser.parse_args()

if len(Args) != 2:
	print >> sys.stderr, 'Usage: %s [options] <image src> <image dst>' % (sys.argv[0],)
	sys.exit(0)

Src,Dst = Args
Ext = Src[Src.rfind('.'):]

# do not modify image more than this much
ComparePctThreshold = min(ComparePctMax, max(ComparePctMin, Opts.max))

if ComparePctThreshold != Opts.max and Opts.verbose:
	print "Adjusting max %.1f within [%.1f,%.0f] to %.1f..." % (
		Opts.max, ComparePctMin, ComparePctMax, ComparePctThreshold)

Tmp,TmpName = tempfile.mkstemp(Ext)

"""
Efficiently generate the image with the lowest compression quality where compare() >= ComparePctThreshold
"""
qualmin,qualmax = 0,100
while qualmax > qualmin+1:
	qual = math.ceil((qualmax / 2.0) + (qualmin / 2.0))
	convert(qual, Src, TmpName)
	cmppct = compare(Src, TmpName)
	if cmppct > ComparePctThreshold:
		qualmin = qual
	else:
		qualmax = qual
	#print '%.2f@%u' % (cmppct, qual),
	#sys.stdout.flush()
#print ''

k0 = os.path.getsize(Src) / 1024.0
k1 = os.path.getsize(TmpName) / 1024.0
kdiff = (k0 - k1) / k0 * 100.0

if kdiff > 0:
	shutil.copy(TmpName, Dst)
	if Opts.verbose:
		print "Before:%.0fKB After:%.0fKB Saved:%.0fKB(%4.1f%%)" % (k0, k1, k0-k1, kdiff)
else:
	shutil.copy(Src, Dst)
	if Opts.verbose:
		print "Couldn't minimize within %.1f%%, using original." % (ComparePctThreshold,)

os.remove(TmpName) # clean up after mkstemp

