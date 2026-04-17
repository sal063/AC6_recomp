cabextract 1.11 - a program to extract Microsoft Cabinet files.
(C) 2000-2023 Stuart Caie <kyzer@cabextract.org.uk>
This is free software with ABSOLUTELY NO WARRANTY.

Cabinet (.CAB) files are a form of archive, which Microsoft use to
distribute their software, and things like Windows Font Packs. The
cabextract program unpacks these files.

For more information, see https://www.cabextract.org.uk/
or run the command 'cabextract --help'.

Microsoft cabinet files should not be confused with InstallShield cabinet
files. InstallShield files are generally called "_sys.cab", "data1.hdr"
"data1.cab", "data2.cab" and so on, and are found in the same directory as
"setup.exe". They begin with the magic characters "ISc(" rather than
"MSCF". cabextract will print the message "This is probably an
InstallShield file." when it finds such a file. The file "doc/magic" in
the cabextract source archive includes additional file-identification
rules for the UNIX file(1) command, which distinguishes between Microsoft
and InstallShield cabinet files.

Example usage:

Extracting files from a cabinet file:
$ cabextract wibble.cab

Extracting files from an executable which contains a cabinet file:
$ cabextract wibble.exe
[cabextract will automatically search executables for embedded cabinets]

Extracting files from a set of cabinet files; wib01.cab, wib02.cab, ...:
$ cabextract wib01.cab
[cabextract will automatically get the names of the other files]

Extracting files to a directory of your choice (in this case, 'boogie'):
$ cabextract -d boogie wibble.cab
[cabextract will create the directory if it does not already exist]

Extracting files that match a filename pattern:
$ cabextract -F *.avi -F *.mpg movies.cab

Listing files from a cabinet file:
$ cabextract -l wibble.cab

Testing the integrity of a cabinet file, without extracting it:
$ cabextract -t wibble.cab
