/* cabinfo -- dumps useful information from cabinets
 * (C) 2000-2018 Stuart Caie <kyzer@cabextract.org.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

/* include some headers from the cut-down copy of libmspack
 * - mspack.h for MSCAB_ATTRIB_?? defines
 * - macros.h for LD and EndGetI?? macros
 * - cab.h for cab structure offsets
 * cabinfo does not use the system-wide <mspack.h> nor does it
 * link with any libmspack functions. It's a standalone program.
 */
#include "mspack/mspack.h"
#include "mspack/macros.h"
#include "mspack/cab.h"

#if HAVE_FSEEKO
# define FSEEK fseeko
# define FTELL ftello
# define FILELEN off_t
#else
# define FSEEK fseek
# define FTELL ftell
# define FILELEN long
#endif

void search();
void getinfo(FILELEN base_offset);
char *read_name();

FILE *fh;
char *filename;
FILELEN filelen;

int main(int argc, char *argv[]) {
    int i;

    printf("Cabinet information dumper by Stuart Caie <kyzer@cabextract.org.uk>\n");

    if (argc <= 1) {
        printf("Usage: %s <file.cab>\n", argv[0]);
        return 1;
    }

    for (i = 1; i < argc; i++) {
        if ((fh = fopen((filename = argv[i]), "rb"))) {
            search();
            fclose(fh);
        }
        else {
            perror(filename);
        }
    }
    return 0;
}

#define MIN(a,b) ((a)<(b)?(a):(b))
#define GETOFFSET      (FTELL(fh))
#define READ(buf,len)  if (myread((void *)(buf),(len))) return
#define SKIP(offset)   if (FSEEK(fh,(offset),SEEK_CUR)) return
#define SEEK(offset)   if (FSEEK(fh,(offset),SEEK_SET)) return
int myread(void *buf, size_t length) {
    length = MIN(length, (int)(filelen - GETOFFSET));
    return fread(buf, 1, length, fh) != length;
}

#define SEARCH_SIZE (32*1024)
unsigned char search_buf[SEARCH_SIZE];

void search() {
    unsigned char *pstart = &search_buf[0], *pend, *p;
    FILELEN offset, caboff, length;
    unsigned long cablen32 = 0, foffset32 = 0;
    int state = 0;

    if (FSEEK(fh, 0, SEEK_END) != 0) {
        perror(filename);
        return;
    }
    filelen = FTELL(fh);
    if (FSEEK(fh, 0, SEEK_SET) != 0) {
        perror(filename);
        return;
    }

    printf("Examining file \"%s\" (%"LD" bytes)...\n", filename, filelen);

    for (offset = 0; offset < filelen; offset += length) {
        /* search length is either the full length of the search buffer,
         * or the amount of data remaining to the end of the file,
         * whichever is less. */
        length = filelen - offset;
        if (length > SEARCH_SIZE) length = SEARCH_SIZE;

        /* fill the search buffer with data from disk */
        SEEK(offset);
        READ(&search_buf[0], length);

        /* read through the entire buffer. */
        p = pstart;
        pend = &search_buf[length];
        while (p < pend) {
            switch (state) {
            /* starting state */
            case 0:
                /* we spend most of our time in this while loop, looking for
                 * a leading 'M' of the 'MSCF' signature */
                while (*p++ != 0x4D && p < pend);
                if (p < pend) state = 1; /* if we found the 'M', advance state */
            break;
        
            /* verify that the next 3 bytes are 'S', 'C' and 'F' */
            case 1: state = (*p++ == 0x53) ? 2 : 0; break;
            case 2: state = (*p++ == 0x43) ? 3 : 0; break;
            case 3: state = (*p++ == 0x46) ? 4 : 0; break;
        
            /* we don't care about bytes 4-7 */
            /* bytes 8-11 are the overall length of the cabinet */
            case 8:  cablen32  = *p++;       state++; break;
            case 9:  cablen32 |= *p++ << 8;  state++; break;
            case 10: cablen32 |= *p++ << 16; state++; break;
            case 11: cablen32 |= *p++ << 24; state++; break;
        
            /* we don't care about bytes 12-15 */
            /* bytes 16-19 are the offset within the cabinet of the filedata */
            case 16: foffset32  = *p++;       state++; break;
            case 17: foffset32 |= *p++ << 8;  state++; break;
            case 18: foffset32 |= *p++ << 16; state++; break;
            case 19: foffset32 |= *p++ << 24;
                /* now we have recieved 20 bytes of potential cab header. */
                /* work out the offset in the file of this potential cabinet */
                caboff = offset + (p-pstart) - 20;
                /* check that the files offset is less than the alleged length
                 * of the cabinet */
                if (foffset32 < cablen32) {
                    /* found a potential result - try loading it */
                    getinfo(caboff);
                    offset = caboff + (FILELEN) cablen32;
                    length = 0;
                    p = pend;
                }
                state = 0;
                break;

            default:
                p++, state++; break;
            } /* switch state */
        } /* while p < pend */
    } /* while offset < filelen */
}

#define GETLONG(n) EndGetI32(&buf[n])
#define GETWORD(n) EndGetI16(&buf[n])
#define GETBYTE(n) ((int)buf[n])

void getinfo(FILELEN base_offset) {
    unsigned char buf[64];
    int header_res = 0, folder_res = 0, data_res = 0;
    int num_folders, num_files, flags, i, j;
    FILELEN files_offset, min_data_offset = filelen;

    SEEK(base_offset);
    READ(&buf, cfhead_SIZEOF);

    files_offset = base_offset + GETLONG(cfhead_FileOffset);
    num_folders = GETWORD(cfhead_NumFolders),
    num_files = GETWORD(cfhead_NumFiles),
    flags = GETWORD(cfhead_Flags);

    printf("CABINET HEADER @%"LD":\n", base_offset);
    printf("- signature      = '%4.4s'\n", buf);
    printf("- overall length = %u bytes\n", GETLONG(cfhead_CabinetSize));
    printf("- files offset   = %"LD"\n", files_offset);
    printf("- format version = %d.%d\n",
        GETBYTE(cfhead_MajorVersion), GETBYTE(cfhead_MinorVersion));
    printf("- folder count   = %u\n", num_folders);
    printf("- file count     = %u\n", num_files);
    printf("- header flags   = 0x%04x%s%s%s\n", flags,
        ((flags & cfheadPREV_CABINET)    ? " PREV_CABINET"    : ""),
        ((flags & cfheadNEXT_CABINET)    ? " NEXT_CABINET"    : ""),
        ((flags & cfheadRESERVE_PRESENT) ? " RESERVE_PRESENT" : ""));
    printf("- set ID         = %u\n", GETWORD(cfhead_SetID));
    printf("- set index      = %u\n", GETWORD(cfhead_CabinetIndex));

    if (flags & cfheadRESERVE_PRESENT) {
        READ(&buf, cfheadext_SIZEOF);
        header_res = GETWORD(cfheadext_HeaderReserved);
        folder_res = GETBYTE(cfheadext_FolderReserved);
        data_res   = GETBYTE(cfheadext_DataReserved);
        printf("- header reserve = %u bytes (@%"LD")\n",
            header_res, GETOFFSET);
        printf("- folder reserve = %u bytes\n", folder_res);
        printf("- data reserve   = %u bytes\n", data_res);
        SKIP(header_res);
    }
    if (flags & cfheadPREV_CABINET) {
        printf("- prev cabinet   = %s\n", read_name());
        printf("- prev disk      = %s\n", read_name());
    }
    if (flags & cfheadNEXT_CABINET) {
        printf("- next cabinet   = %s\n", read_name());
        printf("- next disk      = %s\n", read_name());
    }

    printf("FOLDERS SECTION @%"LD":\n", GETOFFSET);
    for (i = 0; i < num_folders; i++) {
        FILELEN folder_offset, data_offset;
        int comp_type, num_blocks, offset_ok;
        char *type_name;

        folder_offset = GETOFFSET;
        READ(&buf, cffold_SIZEOF);
        data_offset = base_offset + GETLONG(cffold_DataOffset);
        num_blocks = GETWORD(cffold_NumBlocks),
        comp_type = GETWORD(cffold_CompType);

        min_data_offset = MIN(data_offset, min_data_offset);
        offset_ok = data_offset < filelen;

        switch (comp_type & cffoldCOMPTYPE_MASK) {
        case cffoldCOMPTYPE_NONE:    type_name = "stored";  break;
        case cffoldCOMPTYPE_MSZIP:   type_name = "MSZIP";   break;
        case cffoldCOMPTYPE_QUANTUM: type_name = "Quantum"; break;
        case cffoldCOMPTYPE_LZX:     type_name = "LZX";     break;
        default:                     type_name = "unknown"; break;
        }

        printf("- folder 0x%04x @%"LD" %u data blocks @%"LD"%s %s compression (0x%04x)\n",
            i, folder_offset, num_blocks, data_offset,
            offset_ok ? "" : " [INVALID OFFSET]",
            type_name, comp_type);

        SEEK(data_offset);
        for (j = 0; j < num_blocks; j++) {
            int clen, ulen;
            if (GETOFFSET > filelen) {
                printf("  - datablock %d @%"LD" [INVALID OFFSET]\n", j, GETOFFSET);
                break;
            }
            READ(&buf, cfdata_SIZEOF);
            clen = GETWORD(cfdata_CompressedSize);
            ulen = GETWORD(cfdata_UncompressedSize);
            printf("  - datablock %d @%"LD" csum=%08x c=%5u u=%5u%s\n",
                j, data_offset, GETLONG(cfdata_CheckSum), clen, ulen,
                ((clen > (32768+6144)) || (ulen > 32768)) ? " INVALID" : "");
            data_offset += cfdata_SIZEOF + data_res + clen;
            SKIP(data_res + clen);
        }
        SEEK(folder_offset + cffold_SIZEOF + folder_res);
    }

    printf("FILES SECTION @%"LD":\n", GETOFFSET);
    if (files_offset != GETOFFSET) {
        printf("INVALID: file offset in header %"LD
            " doesn't match start of files %"LD"\n",
            files_offset, GETOFFSET);
    }

    for (i = 0; i < num_files; i++) {
        FILELEN file_offset = GETOFFSET;
        char *folder_type;
        int attribs, folder;

        if (file_offset > filelen) return;

        READ(&buf, cffile_SIZEOF);
        folder =  GETWORD(cffile_FolderIndex);
        attribs = GETWORD(cffile_Attribs);

        switch (folder) {
        case cffileCONTINUED_PREV_AND_NEXT:
            folder_type = "continued from prev and to next cabinet"; break;
        case cffileCONTINUED_FROM_PREV:
            folder_type = "continued from prev cabinet"; break;
        case cffileCONTINUED_TO_NEXT:
            folder_type = "continued to next cabinet"; break;
        default:
            folder_type = folder >= num_folders
                ? "INVALID FOLDER INDEX"
                : "normal folder";
            break;
        }

        printf("- file %-5d @%-12"LD"%s\n", i, file_offset,
            (file_offset > min_data_offset ? " [INVALID FILE OFFSET]" : ""));
        printf("  - name   = %s%s\n", read_name(),
            (attribs & MSCAB_ATTRIB_UTF_NAME) ? " (UTF-8)" : "");
        printf("  - folder = 0x%04x [%s]\n", folder, folder_type);
        printf("  - length = %u bytes\n", GETLONG(cffile_UncompressedSize));
        printf("  - offset = %u bytes\n", GETLONG(cffile_FolderOffset));
        printf("  - date   = %02d/%02d/%4d %02d:%02d:%02d\n",
            (GETWORD(cffile_Date))      & 0x1f,
            (GETWORD(cffile_Date) >> 5) & 0xf,
            (GETWORD(cffile_Date) >> 9) + 1980,
            (GETWORD(cffile_Time) >> 11),
            (GETWORD(cffile_Time) >> 5) & 0x3f,
            (GETWORD(cffile_Time) << 1) & 0x3e);
        printf("  - attrs  = 0x%02x %s%s%s%s%s%s\n",
            attribs,
            (attribs & MSCAB_ATTRIB_RDONLY)   ? "RDONLY " : "",
            (attribs & MSCAB_ATTRIB_HIDDEN)   ? "HIDDEN " : "",
            (attribs & MSCAB_ATTRIB_SYSTEM)   ? "SYSTEM " : "",
            (attribs & MSCAB_ATTRIB_ARCH)     ? "ARCH "   : "",
            (attribs & MSCAB_ATTRIB_EXEC)     ? "EXEC "   : "",
            (attribs & MSCAB_ATTRIB_UTF_NAME) ? "UTF-8"   : "");
    }
}

#define CAB_NAMEMAX (1024)
char namebuf[CAB_NAMEMAX];
char *read_name() {
    FILELEN name_start = GETOFFSET;
    int i;
    if (myread(&namebuf, CAB_NAMEMAX)) return "READ FAILED";
    for (i = 0; i <= 256; i++) {
        if (!namebuf[i]) {
            FSEEK(fh, name_start + i + 1, SEEK_SET);
            return namebuf;
        }
    }
    printf("INVALID: name length > 256 for at offset %"LD"\n", name_start);
    namebuf[256] = 0;
    FSEEK(fh, name_start + 257, SEEK_SET);
    return namebuf;
}
