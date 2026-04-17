#!/usr/bin/perl -w
use strict;

my ($blocks1, $blocks2) = map {int} @ARGV;
die <<EOF unless $blocks1 && $blocks2 && $blocks1 < 65536 && $blocks2 < 65536;
Usage: $0 <blocks1> <blocks2>
This generates a CAB file with two files in it, where the first file
is 32768*blocks1 bytes long and the second file is 32768*blocks2 bytes
long. Neither file can be more than 65535*32768 bytes long.
EOF

my $header  = pack('A4V5C2v5', 'MSCF', 0, 0, 0, 0, 0, 3, 1, 2, 2, 0, 0, 0);
my $folders = pack('Vvv', 0, $blocks1, 0)
            . pack('Vvv', 0, $blocks2, 0);
my $files   = pack('V2v4Z*', $blocks1*32768, 0, 0, 0x226C, 0x59BA, 0x20, 'test1.bin')
            . pack('V2v4Z*', $blocks2*32768, 0, 1, 0x226C, 0x59BA, 0x20, 'test2.bin');    
my $block   = pack('VvvC*', 0, 32768, 32768, ((0..255) x 128));


# fixup offsets
my $files_offset  = length($header) + length($folders);
my $blocks_offset = $files_offset   + length($files);
my $cab_length    = $blocks_offset  + length($block) * ($blocks1 + $blocks2);
substr($header,  0x08, 4, pack 'V', $cab_length);
substr($header,  0x10, 4, pack 'V', $files_offset);
substr($folders, 0x00, 4, pack 'V', $blocks_offset);
substr($folders, 0x08, 4, pack 'V', $blocks_offset + length($block) * $blocks1); 

die "Overall cabinet is too large ($cab_length > 4294967295)\n"
    if $cab_length > 4294967295;

# print cab file to stdout
print $header . $folders . $files;
map {print $block} 1 .. ($blocks1 + $blocks2);
