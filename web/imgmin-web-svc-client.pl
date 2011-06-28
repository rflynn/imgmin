#!/usr/bin/env perl
#
# perl client for svc.php
#

use strict;
use warnings;

use LWP;
use Data::Dumper;

my $ua = new LWP::UserAgent(timeout => 5);
my $resp = $ua->post(
        'http://localhost/imgmin/web/svc.php',
        Content_Type => 'multipart/form-data',
        Content => [
                'img' => ['../examples/afghan-girl.jpg'],   # file path
                'MAX_FILE_SIZE' => '10000000',              # required for PHP
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
    print length $resp->content;
}

