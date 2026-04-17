#!/usr/bin/perl -w
use strict;

# reads filenames, from stdin or from files provided on the command ilne,
# writes a cabinet file with those filenames in it to stdout
my @filenames;
while (<>) {
  chomp;
  next if /^$/ or /^#/;
  push @filenames, $_;
}

die "no filenames" if @filenames == 0;
die "too many filenames" if @filenames > 65535;

my (@folders, @files, @datablocks);
my $header = pack 'A4V5C2v5',
   'MSCF', 0, 0, 0,   # signature, 0, cabinet size (fixup), 0,
   0, 0, 3, 1,        # files offset (fixup), 0, format rev, format ver
   1,                 # number of folders
   scalar @filenames, # number of files
   0, 1234, 0;        # flags, set id, set index

push @folders, pack 'Vvv',
    0, # data offset (fixup)
    0, # number of data blocks
    0; # compression method

for my $filename (@filenames) {
    my $attribs = ($filename =~ /[\x80-\xFF]/) ? 0xA0 : 0x20;
    $attribs = 0xA0 if $ENV{FORCE_UTF8};
    $attribs = 0x20 if $ENV{FORCE_CODEPAGE};

    push @files, pack 'V2v4Z*',
        0,         # uncompressed size
        0,         # folder offset
        0,         # folder index
        0x226C,    # time
        0x59BA,    # date
        $attribs,  # attribs
        $filename; # filename
}

push @datablocks, pack 'Vvv',
    0, # checksum
    0, # compressed size
    0; # uncompressed size


# fixup header's cabinet length and files offset
my $files_offset  = length($header) + length(join '', @folders);
my $blocks_offset = $files_offset   + length(join '', @files);
my $cab_length    = $blocks_offset  + length(join '', @datablocks);
substr($header, 0x08, 4, pack 'V', $cab_length);
substr($header, 0x10, 4, pack 'V', $files_offset);

# fixup folders' data block offsets
my $b = 0;
for (my $f = 0; $f < @folders; $f++) {
    substr($folders[$f], 0x00, 4, pack 'V', $blocks_offset);
    my $num_blocks = (unpack 'Vvv', $folders[$f])[1];
    while ($num_blocks-- > 0) {
        $blocks_offset += length($datablocks[$b++]);
    }
}

# print cab file to stdout
print $header, @folders, @files, @datablocks;
