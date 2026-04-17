#!/bin/sh
./filenames.pl case-ascii.txt      >case-ascii.cab
./filenames.pl case-utf8.txt       >case-utf8.cab
./filenames.pl dirwalk-vulns.txt   >dirwalk-vulns.cab
FORCE_CODEPAGE=1 ./filenames.pl encoding-koi8.txt   >encoding-koi8.cab
FORCE_CODEPAGE=1 ./filenames.pl encoding-latin1.txt >encoding-latin1.cab
FORCE_CODEPAGE=1 ./filenames.pl encoding-sjis.txt   >encoding-sjis.cab
FORCE_UTF8=1     ./filenames.pl utf8-stresstest.txt >utf8-stresstest.cab
