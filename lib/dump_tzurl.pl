#!/usr/local/bin/perl

use strict;
use warnings;

my @files = `find $ARGV[0] -name '*.ics' -type f`;
my %tzs;

foreach my $file (@files) {
    chomp ($file);
    open (my $f, '<', $file) or die "couldn't open $file: $!";
    my $tzid;
    my @lines = ();
    my $intz = 0;
    while (<$f>) {
        s/\r\n/\n/g;
        chomp;
        if (/^BEGIN:VTIMEZONE$/) {
            @lines = ($_);
            $intz = 1;
            $tzid = '';
        } elsif (/^END:VTIMEZONE$/) {
            push @lines, $_;
            my $tz = join("\n", @lines, '');
            if (!$tzid) {
                die "timezone without TZID in $file";
            }
            if (defined($tzs{$tzid})) {
                if ($tz ne $tzs{$tzid}) {
                    die "timezone $tzid in $file not the same as before";
                }
            } else {
                $tzs{$tzid} = $tz;
            }
            $intz = 0;
        } elsif ($intz) {
            push @lines, $_;
            if (/^TZID:(.*)$/) {
                $tzid = $1;
            }
        }
    }
    if ($intz) {
        die "timezone $tzid in $file doesn't end";
    }
}

print <<'HEADER';
BEGIN:VCALENDAR
PRODID:-//tzurl.org//NONSGML Olson 2022g//EN
VERSION:2.0
HEADER

foreach my $tz (sort keys %tzs) {
    print $tzs{$tz};
}
print "END:VCALENDAR\n";
