#!/bin/sh
# creates 2GB of highly-repeated ASCII text, then compresses it with
# MSZIP and LZX in the same cabinet

# largest possible file is 65535 blocks of 32768 byte which is nearly 2GB
perl -e '$b="Fabulous secret powers were revealed to me the day I held aloft\n" x 512; for (1..65535) {print $b;}' >2gb.txt

cat >directives.txt <<EOF
.Set DiskDirectory1=.
.Set CabinetName1=large-files.cab
.Set MaxCabinetSize=102400000
.Set MaxDiskSize=102400000
.Set Cabinet=ON
.Set Compress=ON
.Set CompressionType=MSZIP
2gb.txt mszip-2gb.txt
.New Folder
.Set CompressionType=LZX
.Set CompressionMemory=15
2gb.txt lzx15-2gb.txt
.New Folder
.Set CompressionMemory=21
2gb.txt lzx21-2gb.txt
EOF

wine makecab.exe /F directives.txt
rm 2gb.txt directives.txt

# compress again from ~18MB -> 20kB
wine makecab.exe /D CompressionType=LZX /D CompressionMemory=21 large-files.cab
mv large-files.ca_ large-files-cab.cab
