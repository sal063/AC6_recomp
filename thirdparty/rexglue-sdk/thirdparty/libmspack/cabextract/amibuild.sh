#!/bin/sh
# M68K: https://github.com/bebbo/amiga-gcc
# PPC: https://github.com/adtools/amigaos-cross-toolchain

VERSION=`perl -ne 'print $1 if /^AC_INIT.*,\[([\d.]+)\],/' configure.ac`
cat >config.h <<EOF
#define HAVE_MKDIR 1
#define HAVE_UTIME 1
#define HAVE_UTIMES 1
#define HAVE_UTIME_H 1
#define ICONV_CONST const
#define VERSION "$VERSION"
#define WORDS_BIGENDIAN 1
EOF
cp fnmatch_.h fnmatch.h

PATH=/usr/local/amiga/bin:$PATH
CFLAGS='-Wall -O2 -s -DHAVE_CONFIG_H -I. -Imspack'
SRCS="mspack/*.c src/cabextract.c md5.c fnmatch.c"

rm -f *.lha

ppc-amigaos-gcc $CFLAGS -DHAVE_FSEEKO -DHAVE_ICONV -DHAVE_UMASK $SRCS -o cabextract &&
ppc-amigaos-gcc $CFLAGS -DHAVE_FSEEKO src/cabinfo.c -o cabinfo &&
lha a cabextract_OS4.lha cabextract cabinfo

m68k-amigaos-gcc $CFLAGS -noixemul -DLATIN1_FILENAMES $SRCS -o cabextract &&
m68k-amigaos-gcc $CFLAGS -noixemul src/cabinfo.c -o cabinfo &&
lha a cabextract.lha cabextract cabinfo

rm cabextract cabinfo fnmatch.h
