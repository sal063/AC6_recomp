#!/bin/sh
# creates a good mix of standalone folders, folders that
# split backwards, split forwards, and split both ways,
# with differing numbers of files in each
dd if=/dev/urandom of=small1.bin  bs=2000  count=1 2>/dev/null
dd if=/dev/urandom of=small2.bin  bs=8000  count=1 2>/dev/null
dd if=/dev/urandom of=medium1.bin bs=40000 count=1 2>/dev/null
dd if=/dev/urandom of=medium2.bin bs=50000 count=1 2>/dev/null
dd if=/dev/urandom of=small3.bin  bs=128   count=1 2>/dev/null
dd if=/dev/urandom of=medium3.bin bs=40000 count=1 2>/dev/null

# force differing case: wine will write the cab files as "split-1.cab"
# but the cab file will have "Split-1.CAB" embedded in it
touch split-1.cab split-2.cab split-3.cab split-4.cab split-5.cab

cat >directives.txt <<EOF
.Set DiskLabelTemplate="Split cabinet file */5"
.Set DiskDirectoryTemplate=.
.Set CabinetNameTemplate=Split-*.CAB
.Set ClusterSize=1
.Set MaxDiskSize=30000
.Set Cabinet=ON
.Set Compress=ON
; use the "reserved space" feature to check cabextract handles it
.Set ReservePerCabinetSize=100
.Set ReservePerFolderSize=50
.Set ReservePerDataBlockSize=10
small1.bin
.New Folder
small2.bin
medium1.bin
medium2.bin
small3.bin
medium3.bin
EOF

wine makecab.exe /F directives.txt

# makecab.exe messes up and writes "previous file is split-2.cab" instead of
# "previous file is split-3.cab" into header of split-4.cab
sed -e 's/Split-2.CAB/Split-3.CAB/' \
    -e 's!Split cabinet file 2/5!Split cabinet file 3/5!' \
    -i split-4.cab  

# check these against each other
cabextract -t split-1.cab
md5sum small1.bin small2.bin medium1.bin medium2.bin small3.bin medium3.bin

rm *.bin directives.txt setup.inf setup.rpt
