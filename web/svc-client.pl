#!/usr/bin/env perl
#
# perl client for svc.php
#

use strict;
use warnings;

use LWP;
use Data::Dumper;

my $ua = new LWP::UserAgent;
my $resp = $ua->post(
	'http://localhost/imgmin/web/svc.php',
	Content_Type => 'multipart/form-data',
	Content => [
		'img' => ['../examples/afghan-girl.jpg', 'afghan-girl.jpg', undef],
		'MAX_FILE_SIZE' => [undef, undef, '10000000'],
	]);
#print Dumper \$resp;
print length $resp->content();

