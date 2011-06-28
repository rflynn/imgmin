#!/usr/bin/env perl
#
# perl client for svc.php
#

use strict;
use warnings;

use LWP;
use Data::Dumper;

my $filepath = '../examples/afghan-girl.jpg';
my $ua = new LWP::UserAgent(timeout => 5);
my $resp = $ua->post(
        'http://localhost/imgmin/web/svc.php',
        Content_Type => 'multipart/form-data',
        Content => [
                'img' => [$filepath],
                'MAX_FILE_SIZE' => '10000000', # required for PHP
        ]); 
#print Dumper \$resp;
if ($resp->code >= 400)
{
    if ($resp->code == 500 && $resp->headers->{'client-warning'} eq 'Internal response')
    {
        printf "Timeout: %s\n", $resp->code;
    } else {
        printf "Error: %s\n", $resp->code;
    }
    print Dumper $resp->headers;
} else {
    printf "Success: %s\n", $resp->code;
    print Dumper $resp->headers;
    my $oldsize = -s $filepath;
    my $newsize = length $resp->content;
    printf "Before size: %u\n", $oldsize;
    printf "After size: %u (%.1f%% savings)\n", $newsize, 100. - ($newsize / $oldsize * 100.);
}

