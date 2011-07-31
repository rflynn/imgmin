#!/usr/bin/perl
# ex: set ts=8 noet:
#
# Author: Ryan Flynn <parseerror+imgmin@gmail.com>
# imgmin wrapper for apache OutputFilter
# imgmin: https://github.com/rflynn/imgmin
#
# read image file contents from STDIN
# if I haven't seen this file before
#	write it to disk
#	run imgmin.pl on it
# write resulting image contents to STDOUT

use strict;
use warnings;

use Digest::MD5 qw(md5_hex);
use File::Path qw(make_path);

use constant CACHE_BASEDIR => "/var/imgmin-cache/";
use constant IMGMIN_BASEDIR => "../";

# slurp binary data on STDIN
binmode STDIN;
my $contents = do {
	local $/;
	<STDIN>;
};

my $digest = md5_hex($contents);

my $path = $digest;
$path =~ s/^(..)(..)(..)(.*)$/$1\/$2\/$3\/$4/;
$path = CACHE_BASEDIR . $path;

if (! -e $path)
{
	my ($dir) = ($path =~ m#^(.*/)#);
	make_path($dir);
	open(F, ">$path") or die "$path: $!";
	print F $contents;
	close F;
	system((IMGMIN_BASEDIR . "imgmin.pl", $path, $path));
}

binmode STDOUT;
exec "cat $path";

