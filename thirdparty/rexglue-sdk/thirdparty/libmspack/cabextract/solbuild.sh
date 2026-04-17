#!/bin/sh
# packages Solaris version of cabextract, once built
ARCH=`uname -p`
VERSION=`awk -F'"' '/ VERSION /{print$2}' config.h`
echo "packaging cabextract version $VERSION for $ARCH"

rm -rf solaris
mkdir -p solaris/bin solaris/build solaris/man/man1

cp cabextract solaris/bin/
cp doc/cabextract.1 solaris/man/man1/

cat >solaris/pkginfo <<EOF
PKG=GNUcabextract
NAME=cabextract
DESC=Free software for extracting Microsoft cabinet files
CLASSES=none
VERSION=$VERSION
ARCH=$ARCH
CATEGORY=application
VENDOR=https://www.cabextract.org.uk/
EMAIL=kyzer@cabextract.org.uk
BASEDIR=/usr/local
EOF

cat >solaris/prototype <<EOF
i pkginfo
d none bin                   0755 root root
f none bin/cabextract        0755 root root
d none man                   0755 root root
d none man/man1              0755 root root
f none man/man1/cabextract.1 0644 root root
EOF

pkgmk -o -r solaris -d solaris/build -f solaris/prototype
pkgtrans -s solaris/build "$PWD/cabextract-$VERSION-$ARCH-local" GNUcabextract
gzip -9 cabextract-$VERSION-$ARCH-local
