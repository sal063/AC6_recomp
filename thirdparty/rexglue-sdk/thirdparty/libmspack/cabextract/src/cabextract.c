/* cabextract - a program to extract Microsoft Cabinet files
 * (C) 2000-2023 Stuart Caie <kyzer@cabextract.org.uk>
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

/* cabextract uses libmspack to access cabinet files. libmspack is
 * available from https://www.cabextract.org.uk/libmspack/
 */

#define _GNU_SOURCE 1

#if HAVE_CONFIG_H
# include <config.h>
#endif

#include <sys/types.h>

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fnmatch.h>
#include <limits.h>
#include <locale.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_STRINGS_H
# include <strings.h> /* BSD defines strcasecmp() here */
#endif
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#if HAVE_ICONV
# include <iconv.h>
#endif
#if HAVE_TOWLOWER
# include <wctype.h>
#endif
#if HAVE_UTIME
# include <utime.h>
#endif
#if HAVE_UTIMES
# include <sys/time.h>
#endif

/* ensure mkdir(pathname, mode) exists */
#if HAVE_MKDIR
# if MKDIR_TAKES_ONE_ARG
#  define mkdir(a, b) mkdir(a)
# endif
#else
# if HAVE__MKDIR
#  define mkdir(a, b) _mkdir(a)
# else
#  error "Don't know how to create a directory on this system."
# endif
#endif

#ifndef FNM_CASEFOLD
# define FNM_CASEFOLD (0)
#endif

#include "getopt.h"

#include <mspack.h>
#include <md5.h>

/* structures and global variables */
struct option optlist[] = {
  { "directory",    1, NULL, 'd' },
#if HAVE_ICONV
  { "encoding",     1, NULL, 'e' },
#endif
  { "fix",          0, NULL, 'f' },
  { "filter",       1, NULL, 'F' },
  { "help",         0, NULL, 'h' },
  { "list",         0, NULL, 'l' },
  { "lowercase",    0, NULL, 'L' },
  { "pipe",         0, NULL, 'p' },
  { "quiet",        0, NULL, 'q' },
  { "single",       0, NULL, 's' },
  { "test",         0, NULL, 't' },
  { "version",      0, NULL, 'v' },
  { "interactive",  0, NULL, 'i' },
  { "no-overwrite", 0, NULL, 'n' },
  { "keep-symlinks",0, NULL, 'k' },
  { NULL,           0, NULL,  0  }
};

#if HAVE_ICONV
const char *OPTSTRING = "d:e:fF:hlLpqstvink";
#else
const char *OPTSTRING = "d:fF:hlLpqstvink";
#endif

struct file_mem {
  struct file_mem *next;
  dev_t st_dev;
  ino_t st_ino;
  char *from;
};

struct filter {
  struct filter *next;
  char *filter;
};

struct cabextract_args {
  int help, lower, pipe, view, quiet, single, fix, test, interactive,
      no_overwrite, keep_symlinks;
  char *dir, *encoding;
  struct filter *filters;
};

/* global variables */
struct mscab_decompressor *cabd = NULL;

struct file_mem *cab_args = NULL;
struct file_mem *cab_exts = NULL;
struct file_mem *cab_seen = NULL;

mode_t user_umask = 0;

/* answer from user to "overwrite file?" prompt */
char answer[8] = "";

struct cabextract_args args = {
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  NULL, NULL, NULL
};

#if HAVE_ICONV
iconv_t converter = NULL;
/* names for the UTF-8 charset recognised by different iconv_open()s */
char *utf8_names[] = {
    "UTF-8", /* glibc, libiconv, FreeBSD, Solaris, not newlib or HPUX */
    "UTF8",  /* glibc, libiconv (< 1.13), newlib, HPUX */
    "UTF_8", /* newlib, Solaris */
};
#endif

/** A special filename. Extracting to this filename will send the output
 * to standard output instead of a file on disk. The magic happens in
 * cabx_open() when the STDOUT_FNAME pointer is given as a filename, so
 * treat this like a constant rather than a string.
 */
const char *STDOUT_FNAME = "stdout";

/** A special filename. Extracting to this filename will send the output
 * through an MD5 checksum calculator, instead of a file on disk. The
 * magic happens in cabx_open() when the TEST_FNAME pointer is given as a
 * filename, so treat this like a constant rather than a string. 
 */
const char *TEST_FNAME = "test";

/** A global MD5 context, used when a file is written to TEST_FNAME */
struct md5_ctx md5_context;

/** The resultant MD5 checksum, used when a file is written to TEST_FNAME */
unsigned char md5_result[16];

/* prototypes */
static int process_cabinet(char *cabname);
static void load_spanning_cabinets(struct mscabd_cabinet *basecab,
                                   char *basename);
static char *find_cabinet_file(char *origcab, char *cabname);
static int unix_path_seperators(struct mscabd_file *files);
static char *create_output_name(const char *fname, const char *dir,
                                int lower, int isunix, int unicode);
static int can_write(char *fname);
static void set_date_and_perm(struct mscabd_file *file, char *filename);

#if HAVE_ICONV
static void convert_filenames(struct mscabd_file *files);
#endif
#if LATIN1_FILENAMES
static void convert_utf8_to_latin1(char *str);
#endif

static void memorise_file(struct file_mem **fml, char *name, char *from);
static int recall_file(struct file_mem *fml, char *name, char **from);
static void forget_files(struct file_mem **fml);
static void add_filter(char *arg);
static void free_filters();
static int ensure_filepath(char *path, int n);
static char *cab_error(struct mscab_decompressor *cd);
static void print_cli_args_error(const char *reason, const char *exec_name);

static struct mspack_file *cabx_open(struct mspack_system *this,
                                     const char *filename, int mode);
static void cabx_close(struct mspack_file *file);
static int cabx_read(struct mspack_file *file, void *buffer, int bytes);
static int cabx_write(struct mspack_file *file, void *buffer, int bytes);
static int cabx_seek(struct mspack_file *file, off_t offset, int mode);
static off_t cabx_tell(struct mspack_file *file);
static void cabx_msg(struct mspack_file *file, const char *format, ...);
static void *cabx_alloc(struct mspack_system *this, size_t bytes);
static void cabx_free(void *buffer);
static void cabx_copy(void *src, void *dest, size_t bytes);

/**
 * A cabextract-specific implementation of mspack_system that allows
 * the NULL filename to be opened for writing as a synonym for writing
 * to stdout.
 */
static struct mspack_system cabextract_system = {
  &cabx_open, &cabx_close, &cabx_read,  &cabx_write, &cabx_seek,
  &cabx_tell, &cabx_msg, &cabx_alloc, &cabx_free, &cabx_copy, NULL
};

int main(int argc, char *argv[]) {
  int i, err;

  /* attempt to set a UTF8-based locale, so that tolower()/towlower()
   * in create_output_name() lowercase more than just A-Z in ASCII.
   *
   * We don't attempt to pick up the system default locale, "",
   * because it might not be compatible with ASCII/ISO-8859-1/Unicode
   * character codes and would mess up lowercased filenames
   */
  char *locales[] = {
      "C.UTF-8", /* https://sourceware.org/glibc/wiki/Proposals/C.UTF-8 */
      "en_US.UTF-8", "en_GB.UTF8", "de_DE.UTF-8", "UTF-8", "UTF8"
  };
  for (i = 0; i < (sizeof(locales)/sizeof(*locales)); i++) {
      if (setlocale(LC_CTYPE, locales[i])) break;
  }

  /* parse options */
  while ((i = getopt_long(argc, argv, OPTSTRING, optlist, NULL)) != -1) {
    switch (i) {
    case 'd': args.dir              = optarg; break;
    case 'e': args.encoding         = optarg; break;
    case 'f': args.fix              = 1;      break;
    case 'F': add_filter(optarg);             break;
    case 'h': args.help             = 1;      break;
    case 'l': args.view             = 1;      break;
    case 'L': args.lower            = 1;      break;
    case 'p': args.pipe             = 1;      break;
    case 'q': args.quiet            = 1;      break;
    case 's': args.single           = 1;      break;
    case 't': args.test             = 1;      break;
    case 'v': args.view             = 1;      break;
    case 'i': args.interactive      = 1;      break;
    case 'n': args.no_overwrite     = 1;      break;
    case 'k': args.keep_symlinks    = 1;      break;
    }
  }

  if (args.help) {
    fprintf(stderr,
      "Usage: %s [options] [-d dir] <cabinet file(s)>\n\n"
      "This will extract all files from a cabinet or executable cabinet.\n"
      "For multi-part cabinets, only specify the first file in the set.\n\n",
      argv[0]);
    fprintf(stderr,
      "Options:\n"
      "  -v   --version       print version / list cabinet\n"
      "  -h   --help          show this help page\n"
      "  -l   --list          list contents of cabinet\n"
      "  -t   --test          test cabinet integrity\n"
      "  -q   --quiet         only print errors and warnings\n"
      "  -L   --lowercase     make filenames lowercase\n"
      "  -f   --fix           salvage as much as possible from corrupted cabinets\n"
      "  -i   --interactive   prompt whether to overwrite existing files\n"
      "  -n   --no-overwrite  don't overwrite (skip) existing files\n"
      "  -k   --keep-symlinks follow symlinked files/dirs when extracting\n");
    fprintf(stderr,
      "  -p   --pipe          pipe extracted files to stdout\n"
      "  -s   --single        restrict search to cabs on the command line\n"
      "  -F   --filter        extract only files that match the given pattern\n"
#if HAVE_ICONV
      "  -e   --encoding      assume non-UTF8 filenames have the given encoding\n"
#endif
      "  -d   --directory     extract all files to the given directory\n\n"
      "cabextract %s (C) 2000-2023 Stuart Caie <kyzer@cabextract.org.uk>\n"
      "This is free software with ABSOLUTELY NO WARRANTY.\n",
      VERSION);
    return EXIT_FAILURE;
  }

  if (args.test && args.view) {
    print_cli_args_error("You cannot use --test and --list at the same time.",
                         argv[0]);
    return EXIT_FAILURE;
  }

  if (args.interactive) {
    if (args.no_overwrite) {
      print_cli_args_error("Option --interactive can't be used with"
                           " --no-overwrite.", argv[0]);
      return EXIT_FAILURE;
    }

    if (args.pipe) {
      print_cli_args_error("Option --interactive cannot be used with --pipe.",
                           argv[0]);
      return EXIT_FAILURE;
    }
  }

  if (optind == argc) {
    /* no arguments other than the options */
    if (args.view) {
      printf("cabextract version %s\n", VERSION);
      return 0;
    }
    else {
        print_cli_args_error("No cabinet files specified.", argv[0]);
      return EXIT_FAILURE;
    }
  }

  /* memorise command-line cabs if necessary */
  if (args.single) {
    for (i = optind; i < argc; i++) memorise_file(&cab_args, argv[i], NULL);
  }

  /* extracting to stdout implies shutting up on stdout */
  if (args.pipe && !args.view) args.quiet = 1;

  /* open libmspack */
  MSPACK_SYS_SELFTEST(err);
  if (err) {
    if (err == MSPACK_ERR_SEEK) {
      fprintf(stderr,
              "FATAL ERROR: libmspack is compiled for %d-bit file IO,\n"
              "             cabextract is compiled for %d-bit file IO.\n",
              (sizeof(off_t) == 4) ? 64 : 32,
              (sizeof(off_t) == 4) ? 32 : 64);
    }
    else {
      fprintf(stderr, "FATAL ERROR: libmspack self-test returned %d\n", err);
    }
    return EXIT_FAILURE;
  }

  if (!(cabd = mspack_create_cab_decompressor(&cabextract_system))) {
    fprintf(stderr, "can't create libmspack CAB decompressor\n");
    return EXIT_FAILURE;
  }

  /* obtain user's umask */
#if HAVE_UMASK
  umask(user_umask = umask(0));
#endif

  /* turn on/off 'fix MSZIP' and 'salvage' mode */
  cabd->set_param(cabd, MSCABD_PARAM_FIXMSZIP, args.fix);
  cabd->set_param(cabd, MSCABD_PARAM_SALVAGE, args.fix);

#if HAVE_ICONV
  /* set up converter from given encoding to UTF-8 */
  if (args.encoding) {
    for (i = 0; i < (sizeof(utf8_names)/sizeof(*utf8_names)); i++) {
      converter = iconv_open(utf8_names[i], args.encoding);
      if (converter != (iconv_t) -1) break;
    }
    if (converter == (iconv_t) -1) {
      fprintf(stderr, "FATAL ERROR: encoding '%s' is not recognised\n",
          args.encoding);
      return EXIT_FAILURE;
    }
  }
#endif

  /* process cabinets */
  for (i = optind, err = 0; i < argc; i++) {
    err += process_cabinet(argv[i]);
  }

  /* error summary */
  if (!args.quiet) {
    if (err) printf("\nAll done, errors in processing %d file(s)\n", err);
    else printf("\nAll done, no errors.\n");
  }

#if HAVE_ICONV
  if (converter) {
    iconv_close(converter);
  }
#endif

  /* close libmspack */
  mspack_destroy_cab_decompressor(cabd);

  /* empty file-memory lists */
  forget_files(&cab_args);
  forget_files(&cab_exts);
  forget_files(&cab_seen);

  return err ? EXIT_FAILURE : EXIT_SUCCESS;
}

/**
 * Processes each file argument on the command line, as specified by the
 * command line options. This does the main bulk of work in cabextract.
 *
 * @param basename the file to process
 * @return the number of files with errors, usually 0 for success or 1 for
 *         failure
 */
static int process_cabinet(char *basename) {
  struct mscabd_cabinet *basecab, *cab, *cab2;
  struct mscabd_file *file;
  int isunix, fname_offset, viewhdr = 0;
  char *from, *name;
  int errors = 0;

  /* do not process repeat cabinets */
  if (recall_file(cab_seen, basename, &from) ||
      recall_file(cab_exts, basename, &from)) {
    if (!args.quiet) {
      if (!from) printf("%s: skipping known cabinet\n", basename);
      else printf("%s: skipping known cabinet (from %s)\n", basename, from);
    }
    return 0; /* return success */
  }
  memorise_file(&cab_seen, basename, NULL);

  /* search the file for cabinets */
  if (!(basecab = cabd->search(cabd, basename))) {
    if (cabd->last_error(cabd)) {
      fprintf(stderr, "%s: %s\n", basename, cab_error(cabd));
    }
    else {
      fprintf(stderr, "%s: no valid cabinets found\n", basename);
    }
    return 1;
  }

  /* iterate over all cabinets found in that file */
  for (cab = basecab; cab; cab = cab->next) {

    /* load all spanning cabinets */
    load_spanning_cabinets(cab, basename);

#if HAVE_ICONV
    /* convert all non-UTF8 filenames to UTF8 using given encoding */
    if (converter) convert_filenames(cab->files);
#endif

    /* determine whether UNIX or MS-DOS path seperators are used */
    isunix = unix_path_seperators(cab->files);

    /* print headers */
    if (!viewhdr) {
      if (args.view) {
        if (!args.quiet) printf("Viewing cabinet: %s\n", basename);
        printf(" File size | Date       Time     | Name\n");
        printf("-----------+---------------------+-------------\n");
      }
      else {
        if (!args.quiet) {
          printf("%s cabinet: %s\n", args.test ? "Testing" : "Extracting",
                                     basename);
        }
      }
      viewhdr = 1;
    }

    /* the full UNIX output filename includes the output
     * directory. However, for filtering purposes, we don't want to
     * include that. So, we work out where the filename part of the
     * output name begins. This is the same for every extracted file.
     */
    fname_offset = args.dir ? (strlen(args.dir) + 1) : 0;

    /* process all files */
    for (file = cab->files; file; file = file->next) {
      /* create the full UNIX output filename */
      if (!(name = create_output_name(file->filename, args.dir,
            args.lower, isunix, file->attribs & MSCAB_ATTRIB_UTF_NAME)))
      {
        errors++;
        continue;
      }

      /* if filtering, do so now. skip if file doesn't match any filter */
      if (args.filters) {
        int matched = 0;
        struct filter *f;
        for (f = args.filters; f; f = f->next) {
          if (!fnmatch(f->filter, &name[fname_offset], FNM_CASEFOLD)) {
            matched = 1;
            break;
          }
        }
        if (!matched) {
          free(name);
          continue;
        }
      }

      /* view, extract or test the file */
      if (args.view) {
        printf("%10u | %02d.%02d.%04d %02d:%02d:%02d | %s\n",
               file->length, file->date_d, file->date_m, file->date_y,
               file->time_h, file->time_m, file->time_s, name);
      }
      else if (args.test) {
        if (cabd->extract(cabd, file, TEST_FNAME)) {
          /* file failed to extract */
          printf("  %s  failed (%s)\n", name, cab_error(cabd));
          errors++;
        }
        else {
          /* file extracted OK, print the MD5 checksum in md5_result. Print
           * the checksum right-aligned to 79 columns if that's possible,
           * otherwise just print it 2 spaces after the filename and "OK" */

          /* "  filename  OK  " is 8 chars + the length of filename,
           * the MD5 checksum itself is 32 chars. */
          int spaces = 79 - (strlen(name) + 8 + 32);
          printf("  %s  OK  ", name);
          while (spaces-- > 0) putchar(' ');
          printf("%02x%02x%02x%02x%02x%02x%02x%02x"
                 "%02x%02x%02x%02x%02x%02x%02x%02x\n",
                 md5_result[0], md5_result[1], md5_result[2], md5_result[3],
                 md5_result[4], md5_result[5], md5_result[6], md5_result[7],
                 md5_result[8], md5_result[9], md5_result[10],md5_result[11],
                 md5_result[12],md5_result[13],md5_result[14],md5_result[15]);
        }
      }
      else {
        /* extract the file */
        if (args.pipe) {
          /* extracting to stdout */
          if (cabd->extract(cabd, file, STDOUT_FNAME)) {
            fprintf(stderr, "%s(%s): %s\n", STDOUT_FNAME, name,
                                            cab_error(cabd));
            errors++;
          }
        }
        else if (can_write(name)) {
          /* extracting to a regular file */
          if (!args.quiet) printf("  extracting %s\n", name);

          if (!ensure_filepath(name, fname_offset)) {
            fprintf(stderr, "%s: can't create file path\n", name);
            errors++;
          }
          else {
            if (cabd->extract(cabd, file, name)) {
              fprintf(stderr, "%s: %s\n", name, cab_error(cabd));
              errors++;
            }
            else {
              set_date_and_perm(file, name);
            }
          }
        }
        else {
          /* can_write() said no */
          if (!args.quiet) printf("  skipping %s\n", name);
        }
      }
      free(name);
    } /* for (all files in cab) */

    /* free the spanning cabinet filenames [not freed by cabd->close()] */
    for (cab2 = cab->prevcab; cab2; cab2 = cab2->prevcab) free((void*)cab2->filename);
    for (cab2 = cab->nextcab; cab2; cab2 = cab2->nextcab) free((void*)cab2->filename);
  } /* for (all cabs) */

  /* free all loaded cabinets */
  cabd->close(cabd, basecab);
  return errors;
}

/**
 * Follows the spanning cabinet chain specified in a cabinet, loading
 * and attaching the spanning cabinets as it goes.
 *
 * @param basecab  the base cabinet to start the chain from.
 * @param basename the full pathname of the base cabinet, so spanning
 *                 cabinets can be found in the same path as the base cabinet.
 * @see find_cabinet_file()
 */
static void load_spanning_cabinets(struct mscabd_cabinet *basecab,
                                   char *basename)
{
  struct mscabd_cabinet *cab, *cab2;
  char *name;

  /* load any spanning cabinets -- backwards */
  for (cab = basecab; cab->flags & MSCAB_HDR_PREVCAB; cab = cab->prevcab) {
    if (!(name = find_cabinet_file(basename, cab->prevname))) {
      fprintf(stderr, "%s: can't find %s\n", basename, cab->prevname);
      break;
    }
    if (args.single && !recall_file(cab_args, name, NULL)) break;
    if (!args.quiet) {
      printf("%s: extends backwards to %s (%s)\n", basename,
             cab->prevname, cab->previnfo);
    }
    if (!(cab2 = cabd->open(cabd,name)) || cabd->prepend(cabd, cab, cab2)) {
      fprintf(stderr, "%s: can't prepend %s: %s\n", basename,
              cab->prevname, cab_error(cabd));
      if (cab2) cabd->close(cabd, cab2);
      break;
    }
    memorise_file(&cab_exts, name, basename);
  }

  /* load any spanning cabinets -- forwards */
  for (cab = basecab; cab->flags & MSCAB_HDR_NEXTCAB; cab = cab->nextcab) {
    if (!(name = find_cabinet_file(basename, cab->nextname))) {
      fprintf(stderr, "%s: can't find %s\n", basename, cab->nextname);
      break;
    }
    if (args.single && !recall_file(cab_args, name, NULL)) break;
    if (!args.quiet) {
      printf("%s: extends to %s (%s)\n", basename,
             cab->nextname, cab->nextinfo);
    }
    if (!(cab2 = cabd->open(cabd,name)) || cabd->append(cabd, cab, cab2)) {
      fprintf(stderr, "%s: can't append %s: %s\n", basename,
              cab->nextname, cab_error(cabd));
      if (cab2) cabd->close(cabd, cab2);
      break;
    }
    memorise_file(&cab_exts, name, basename);
  }
}

/**
 * Matches a cabinet's filename case-insensitively in the filesystem and
 * returns the case-correct form.
 *
 * @param origcab if this is non-NULL, the pathname part of this filename
 *                will be extracted, and the search will be conducted in
 *                that directory.
 * @param cabname the internal CAB filename to search for.
 * @return a copy of the full, case-correct filename of the given cabinet
 *         filename, or NULL if the specified filename does not exist on disk.
 */
static char *find_cabinet_file(char *origcab, char *cabname) {
  struct dirent *entry;
  struct stat st_buf;
  int found = 0, len;
  char *tail, *cab;
  DIR *dir;

  /* ensure we have a cabinet name at all */
  if (!cabname || !cabname[0]) return NULL;

  /* find if there's a directory path in the origcab */
  tail = origcab ? strrchr(origcab, '/') : NULL;
  len = (tail - origcab) + 1;

  /* allocate memory for our copy */
  if (!(cab = malloc((tail ? len : 2) + strlen(cabname) + 1))) return NULL;

  /* add the directory path from the original cabinet name, or "." */
  if (tail) memcpy(cab, origcab, (size_t) len);
  else      cab[0]='.', cab[1]='/', len=2;
  cab[len] = '\0';

  /* try accessing the cabinet with its current name (case-sensitive) */
  strcpy(&cab[len], cabname);
  if (stat(cab, &st_buf) == 0) {
    found = 1;
  }
  else {
    /* cabinet was not found, look for it in the current dir */
    cab[len] = '\0';
    if ((dir = opendir(cab))) {
      while ((entry = readdir(dir))) {
        if (strcasecmp(cabname, entry->d_name) == 0) {
          strcat(cab, entry->d_name);
          found = (stat(cab, &st_buf) == 0);
          break;
        }
      }
      closedir(dir);
    }
  }

  if (!found || !S_ISREG(st_buf.st_mode)) {
    /* cabinet not found, or not a regular file */
    free(cab);
    cab = NULL;
  }

  return cab;
}

/**
 * Determines whether UNIX '/' or MS-DOS '\' path seperators are used in
 * the cabinet file. The algorithm is as follows:
 *
 * Look at all slashes in all filenames. If there are no slashes, MS-DOS
 * seperators are assumed (it doesn't matter). If all are backslashes,
 * MS-DOS seperators are assumed. If all are forward slashes, UNIX
 * seperators are assumed.
 *
 * If not all slashes are the same, go through each filename, looking for
 * the first slash.  If the part of the filename up to and including the
 * slash matches the previous filename, that kind of slash is the
 * directory seperator.
 *
 * @param files list of files in the cab file
 * @return 0 for MS-DOS seperators, or 1 for UNIX seperators.
 */
static int unix_path_seperators(struct mscabd_file *files) {
  struct mscabd_file *fi;
  char slash=0, backslash=0, *oldname;
  int oldlen;

  for (fi = files; fi; fi = fi->next) {
    char *p;
    for (p = fi->filename; *p; p++) {
      if (*p == '/') slash = 1;
      if (*p == '\\') backslash = 1;
    }
    if (slash && backslash) break;
  }

  if (slash) {
    /* slashes, but no backslashes = UNIX */
    if (!backslash) return 1;
  }
  else {
    /* no slashes = MS-DOS */
    return 0;
  }

  /* special case if there's only one file - just take the first slash */
  if (!files->next) {
    char c, *p = fi->filename;
    while ((c = *p++)) {
      if (c == '\\') return 0; /* backslash = MS-DOS */
      if (c == '/')  return 1; /* slash = UNIX */
    }
    /* should not happen - at least one slash was found! */
    return 0;
  }

  oldname = NULL;
  oldlen = 0;
  for (fi = files; fi; fi = fi->next) {
    char *name = fi->filename;
    int len = 0;
    while (name[len]) {
      if ((name[len] == '\\') || (name[len] == '/')) break;
      len++;
    }
    if (!name[len]) len = 0; else len++;

    if (len && (len == oldlen)) {
      if (strncmp(name, oldname, (size_t) len) == 0)
        return (name[len-1] == '\\') ? 0 : 1;
    }
    oldname = name;
    oldlen = len;
  }

  /* default */
  return 0;
}

/**
 * Creates a UNIX filename from the internal CAB filename and the given
 * parameters.
 *
 * @param fname  the internal CAB filename.
 * @param dir    a directory path to prepend to the output filename.
 * @param lower  if non-zero, filename should be made lower-case.
 * @param isunix if zero, MS-DOS path seperators are used in the internal
 *               CAB filename. If non-zero, UNIX path seperators are used.
 * @param utf8   if non-zero, the internal CAB filename is encoded in UTF-8.
 * @return a freshly allocated and created filename, or NULL if there was
 *         not enough memory.
 * @see unix_path_seperators()
 */
static char *create_output_name(const char *fname, const char *dir,
                                int lower, int isunix, int utf8)
{
  char sep   = (isunix) ? '/'  : '\\'; /* the path-seperator */
  char slash = (isunix) ? '\\' : '/';  /* the other slash */

  size_t dirlen = dir ? strlen(dir) + 1 : 0; /* length of dir + '/' */
  size_t filelen = strlen(fname);

  /* worst case, UTF-8 processing expands all chars to 4 bytes */
  char *name =  malloc(dirlen + (filelen * 4) + 2);

  unsigned char *i    = (unsigned char *) &fname[0];
  unsigned char *iend = (unsigned char *) &fname[filelen];
  unsigned char *o    = (unsigned char *) &name[dirlen], c;

  if (!name) {
    fprintf(stderr, "Can't allocate output filename\n");
    return NULL;
  }

  /* copy directory prefix if needed */
  if (dir) {
    strcpy(name, dir);
    name[dirlen - 1] = '/';
  }

  /* copy cab filename to output name, converting MS-DOS slashes to UNIX
   * slashes as we go. Also lowercases characters if needed. */
  if (utf8) {
    /* handle UTF-8 encoded filenames (see RFC 3629). This doesn't reject bad
     * UTF-8 with overlong encodings, but does re-encode it as valid UTF-8. */
    while (i < iend) {
      /* get next UTF-8 character */
      int x;
      if ((c = *i++) < 0x80) {
        x = c;
      }
      else if (c >= 0xC2 && c < 0xE0 && i <= iend && (i[0] & 0xC0) == 0x80) {
        x = (c & 0x1F) << 6;
        x |= *i++ & 0x3F;
      }
      else if (c >= 0xE0 && c < 0xF0 && i+1 <= iend && (i[0] & 0xC0) == 0x80 &&
               (i[1] & 0xC0) == 0x80)
      {
        x = (c & 0x0F) << 12;
        x |= (*i++ & 0x3F) << 6;
        x |= *i++ & 0x3F;
      }
      else if (c >= 0xF0 && c < 0xF5 && i+2 <= iend && (i[0] & 0xC0) == 0x80 &&
               (i[1] & 0xC0) == 0x80 && (i[2] & 0xC0) == 0x80)
      {
        x = (c & 0x07) << 18;
        x |= (*i++ & 0x3F) << 12;
        x |= (*i++ & 0x3F) << 6;
        x |= *i++ & 0x3F;
      }
      else {
        x = 0xFFFD; /* bad first byte */
      }

      if (x <= 0 || x > 0x10FFFF || (x >= 0xD800 && x <= 0xDFFF) ||
          x == 0xFFFE || x == 0xFFFF)
      {
        x = 0xFFFD; /* invalid code point or cheeky null byte */
      }

#if HAVE_TOWLOWER
      if (lower) x = towlower(x);
#else
      if (lower && x < 256) x = tolower(x);
#endif

      /* whatever is the path separator -> '/'
       * whatever is the other slash    -> '\' */
      if (x == sep) x = '/'; else if (x == slash) x = '\\';

      /* convert unicode character back to UTF-8 */
      if (x < 0x80) {
        *o++ = (unsigned char) x;
      }
      else if (x < 0x800) {
        *o++ = 0xC0 | (x >> 6);
        *o++ = 0x80 | (x & 0x3F);
      }
      else if (x < 0x10000) {
        *o++ = 0xE0 | (x >> 12);
        *o++ = 0x80 | ((x >> 6) & 0x3F);
        *o++ = 0x80 | (x & 0x3F);
      }
      else if (x <= 0x10FFFF) {
        *o++ = 0xF0 | (x >> 18);
        *o++ = 0x80 | ((x >> 12) & 0x3F);
        *o++ = 0x80 | ((x >> 6) & 0x3F);
        *o++ = 0x80 | (x & 0x3F);
      }
      else {
        *o++ = 0xEF; /* unicode replacement character in UTF-8 */
        *o++ = 0xBF;
        *o++ = 0xBD;
      }
    }
    *o++ = '\0';
#if LATIN1_FILENAMES
    convert_utf8_to_latin1(&name[dirlen]);
#endif
  }
  else {
    /* non UTF-8 version */
    while (i < iend) {
      c = *i++;
      if (lower) c = (unsigned char) tolower((int) c);
      if (c == sep) c = '/'; else if (c == slash) c = '\\';
      *o++ = c;
    }
    *o++ = '\0';
  }

  /* remove any leading slashes in the cab filename part.
   * This prevents unintended absolute file path access. */
  o = (unsigned char *) &name[dirlen];
  for (i = o; *i == '/' || *i == '\\'; i++);
  if (i != o) {
    size_t len = strlen((char *) i);
    if (len > 0) {
      memmove(o, i, len + 1);
    }
    else {
      /* change filename composed entirely of leading slashes to "x" */
      strcpy((char *) o, "x");
    }
  }

  /* search for "../" or "..\" in cab filename part and change to "xx"
   * This prevents unintended directory traversal. */
  for (; *o; o++) {
    if ((o[0] == '.') && (o[1] == '.') && (o[2] == '/' || o[2] == '\\')) {
      o[0] = o[1] = 'x';
      o += 2;
    }
  }

  return name;
}

/**
 * Determines if the chosen file can be (over)written. If it doesn't exist,
 * the answer is always yes. But if the file already exists, look at the
 * command-line options to decide what to do:
 *
 * - if args.no_overwrite is set, never overwrite
 * - if args.interactive is set, prompt the user on the terminal
 * - otherwise, always overwrite
 *
 * If choosing to overwrite, normal behaviour is to unlink() the existing
 * file, because if it were a symlink, writing to the file would write to
 * the destination of the symlink. This is suppressed if args.keep_symlinks
 * is set.
 *
 * @param name the name of the UNIX file to (over)write
 * @return 1 if (over)writing should proceed, 0 if file should be skipped
 */
static int can_write(char *name) {
    struct stat st_buf;

    /* if file does not exist, always write */
    if (stat(name, &st_buf) != 0) return 1;

    /* if "-n" is set (no overwrite), always skip */
    if (args.no_overwrite) return 0;

    /* if interactive mode requested, ask the user */
    if (args.interactive) {
        if (answer[0] == 'N') {
            return 0; /* always no */
        }
        else if (answer[0] != 'A') {
            for (;;) {
                printf("replace %s? [y]es, [n]o, [A]ll, [N]one: ", name);
                fflush(stdout);
                /* if fgets() fails (e.g. EOF), assume "no" */
                if (!fgets(answer, sizeof(answer), stdin)) return 0;
                if (answer[0] == 'n' || answer[0] == 'N') return 0;
                if (answer[0] == 'y' || answer[0] == 'A') break;
                printf("invalid response \"%s\", type y, n, A or N\n", answer);
            }
        }
    }

    /* yes, overwrite the file. if "-k" is not set, unlink() it first */
    if (!args.keep_symlinks) {
        if (unlink(name)) {
            fprintf(stderr, "can't remove old %s: %s\n", name, strerror(errno));
            return 0;
        }
    }
    return 1;
}

/**
 * Sets the last-modified time and file permissions on a file.
 *
 * @param file     the internal CAB file whose date, time and attributes will 
 *                 be used.
 * @param filename the name of the UNIX file whose last-modified time and
 *                 file permissions will be set.
 */
static void set_date_and_perm(struct mscabd_file *file, char *filename) {
  mode_t mode;
  struct tm tm;
#if HAVE_UTIME
  struct utimbuf utb;
#elif HAVE_UTIMES
  struct timeval tv[2];
#endif

  /* set last modified date */
  tm.tm_sec   = file->time_s;
  tm.tm_min   = file->time_m;
  tm.tm_hour  = file->time_h;
  tm.tm_mday  = file->date_d;
  tm.tm_mon   = file->date_m - 1;
  tm.tm_year  = file->date_y - 1900;
  tm.tm_isdst = -1;

#if HAVE_UTIME
  utb.actime = utb.modtime = mktime(&tm);
  utime(filename, &utb);
#elif HAVE_UTIMES
  tv[0].tv_sec  = tv[1].tv_sec  = mktime(&tm);
  tv[0].tv_usec = tv[1].tv_usec = 0;
  utimes(filename, &tv[0]);
#endif

  /* set permissions */
  mode = 0444;
  if (  file->attribs & MSCAB_ATTRIB_EXEC)    mode |= 0111;
  if (!(file->attribs & MSCAB_ATTRIB_RDONLY)) mode |= 0222;
  chmod(filename, mode & ~user_umask);
}

#if HAVE_ICONV
static char *convert_filename(char *name) {
    /* worst case: all characters expand from 1 to 4 bytes */
    size_t ilen = strlen(name) + 1, olen = ilen * 4;
    ICONV_CONST char *i = name;
    char *newname = malloc(olen), *o = newname;

    if (!newname) {
        fprintf(stderr, "WARNING: out of memory converting filename\n");
        return NULL;
    }

    /* convert filename to UTF8 */
    iconv(converter, NULL, NULL, NULL, NULL);
    while (iconv(converter, &i, &ilen, &o, &olen) == (size_t) -1) {
        if (errno == EILSEQ || errno == EINVAL) {
            /* invalid or incomplete multibyte sequence: skip it */
            i++; ilen--;
            memcpy(o, "\xEF\xBF\xBD", 3); o += 3; olen += 3;
        }
        else /* E2BIG: should be impossible to get here */ {
            free(newname);
            fprintf(stderr, "WARNING: error while converting filename: %s",
                strerror(errno));
            return NULL;
        }
    }
    return newname;
}

static void convert_filenames(struct mscabd_file *files) {
    struct mscabd_file *fi;
    for (fi = files; fi; fi = fi->next) {
        if (!(fi->attribs & MSCAB_ATTRIB_UTF_NAME)) {
            char *newname = convert_filename(fi->filename);
            if (newname) {
                /* replace filename with converted filename - this is a dirty
                 * hack to avoid having to convert filenames twice (first for
                 * unix_path_seperators(), then again for create_output_name())
                 * Instead of obeying the libmspack API and treating
                 * fi->filename as read only, we know libmspack allocated it
                 * using cabx_alloc() which uses malloc(), so we can free() it
                 * and replace it with other memory allocated with malloc()
                 */
                free(fi->filename);
                fi->filename = newname;
                fi->attribs |= MSCAB_ATTRIB_UTF_NAME;
            }
        }
    }
}
#endif

#if LATIN1_FILENAMES
/* converts _valid_ UTF-8 to ISO-8859-1 in-place */
static void convert_utf8_to_latin1(char *str) {
    unsigned char *i = (unsigned char *) str, *o = i, c;
    while ((c = *i++)) {
        if (c < 0x80) {
            *o++ = c;
        }
        else if (c == 0xC2 || c == 0xC3) {
            *o++ = ((c << 6) & 0x03) | (*i++ | 0x3F);
        }
        else {
            *o++ = '?';
            i += (c >= 0xF0) ? 3 : (c >= 0xE0) ? 2 : 1;
        }
    }
    *o = '\0';
}
#endif

/* ------- support functions ------- */

/**
 * Memorises a file by its device and inode number rather than its name. If
 * the file does not exist, it will not be memorised.
 *
 * @param fml  address of the file_mem list that will memorise this file.
 * @param name name of the file to memorise.
 * @param from a string that, if not NULL, will be duplicated stored with
 *             the memorised file.
 * @see recall_file(), forget_files()
 */
static void memorise_file(struct file_mem **fml, char *name, char *from) {
  struct file_mem *fm;
  struct stat st_buf;
  if (stat(name, &st_buf) != 0) return;
  if (!(fm = malloc(sizeof(struct file_mem)))) return;
  fm->st_dev = st_buf.st_dev;
  fm->st_ino = st_buf.st_ino;
  fm->from = (from) ? malloc(strlen(from)+1) : NULL;
  if (fm->from) strcpy(fm->from, from);
  fm->next = *fml;
  *fml = fm;
}

/**
 * Determines if a file has been memorised before, by its device and inode
 * number. If the file does not exist, it cannot be recalled.
 *
 * @param fml  list to search for previously memorised file
 * @param name name of file to recall.
 * @param from if non-NULL, this is an address that the associated "from"
 *             description pointer will be stored.
 * @return non-zero if the file has been previously memorised, zero if the
 *         file is unknown or does not exist.
 * @see memorise_file(), forget_files()
 */
static int recall_file(struct file_mem *fml, char *name, char **from) {
  struct file_mem *fm;
  struct stat st_buf;
  if (stat(name, &st_buf) != 0) return 0;
  for (fm = fml; fm; fm = fm->next) {
    if ((st_buf.st_ino == fm->st_ino) && (st_buf.st_dev == fm->st_dev)) {
      if (from) *from = fm->from;
      return 1;
    }
  }
  return 0;
}

/**
 * Frees all memory used by a file_mem list.
 *
 * @param fml address of the list to free
 * @see memorise_file()
 */
static void forget_files(struct file_mem **fml) {
  struct file_mem *fm, *next;
  for (fm = *fml; fm; fm = next) {
    next = fm->next;
    free(fm->from);
    free(fm);
  }
  *fml = NULL;
}

/**
 * Adds a filter to args.filters. On first call, sets up
 * free_filters() to run at exit.
 *
 * @param arg filter to add
 */
static void add_filter(char *arg) {
    struct filter *f = malloc(sizeof(struct filter));
    if (f) {
        if (!args.filters) {
            atexit(free_filters);
        }
        f->next = args.filters;
        f->filter = arg;
        args.filters = f;
    }
}

/** Frees all memory used by args.filters */
static void free_filters() {
    struct filter *f, *next;
    for (f = args.filters; f; f = next) {
        next = f->next;
        free(f);
    }
}

/**
 * Ensures that all directory components in a filepath exist. New directory
 * components are created, if necessary.
 *
 * If args.keep_symlinks is not set, any symlink found the path (that comes
 * after n chars) will be deleted to make way for a directory.
 *
 * @param path the filepath to check
 * @param n the number of chars of path NOT to test for symlinks
 * @return non-zero if all directory components in a filepath exist, zero
 *         if components do not exist and cannot be created
 */
static int ensure_filepath(char *path, int n) {
  struct stat st_buf;
  char *p, *parchive = &path[n];
  int ok;

  for (p = &path[1]; *p; p++) {
    if (*p != '/') continue;
    *p = '\0';
    if (p < parchive || args.keep_symlinks) {
      /* not in the archive-determined part of the path, or keeping symlinks:
       * merely check if this path element is a directory */
      ok = (stat(path, &st_buf) == 0) && S_ISDIR(st_buf.st_mode);
    }
    else {
      /* in the archive-determined part of the path and not keeping symlinks:
       * use lstat() and delete symlinks if found */
      ok = (lstat(path, &st_buf) == 0);
      if (ok) {
        if ((st_buf.st_mode & S_IFMT) == S_IFLNK) unlink(path);
        ok = S_ISDIR(st_buf.st_mode);
      }
    }
    if (!ok) ok = (mkdir(path, 0777 & ~user_umask) == 0);
    *p = '/';
    if (!ok) return 0;
  }
  return 1;
}

/**
 * Utility for printing wrong CLI args error messages.
 */
static void print_cli_args_error(const char *reason, const char *exec_name) {
  fprintf(stderr, "%s: %s\nTry '%s --help' for more information.\n",
          exec_name, reason, exec_name);
}

/**
 * Returns a string with an error message appropriate for the last error
 * of the CAB decompressor.
 *
 * @param  cd the CAB decompressor.
 * @return a constant string with an appropriate error message.
 */
static char *cab_error(struct mscab_decompressor *cd) {
  switch (cd->last_error(cd)) {
  case MSPACK_ERR_OPEN:
    return errno ? strerror(errno) : "file open error";
  case MSPACK_ERR_READ:
    return errno ? strerror(errno) : "file read error";
  case MSPACK_ERR_WRITE:
    return errno ? strerror(errno) : "file write error";
  case MSPACK_ERR_SEEK:
    return errno ? strerror(errno) : "file seek error";
  case MSPACK_ERR_NOMEMORY:
    return "out of memory";
  case MSPACK_ERR_SIGNATURE:
    return "bad CAB signature";
  case MSPACK_ERR_DATAFORMAT:
    return "error in CAB data format";
  case MSPACK_ERR_CHECKSUM:
    return "checksum error";
  case MSPACK_ERR_DECRUNCH:
    return "decompression error";
  }
  return "unknown error";
}

struct mspack_file_p {
  FILE *fh;
  const char *name;
  char regular_file;
};

static struct mspack_file *cabx_open(struct mspack_system *this,
                                    const char *filename, int mode)
{
  struct mspack_file_p *fh;
  const char *fmode;

  /* Use of the STDOUT_FNAME pointer for a filename means the file should
   * actually be extracted to stdout. Use of the TEST_FNAME pointer for a
   * filename means the file should only be MD5-summed.
   */
  if (filename == STDOUT_FNAME || filename == TEST_FNAME) {
    /* only WRITE mode is valid for these special files */
    if (mode != MSPACK_SYS_OPEN_WRITE) {
      return NULL;
    }
  }

  /* ensure that mode is one of READ, WRITE, UPDATE or APPEND */
  switch (mode) {
  case MSPACK_SYS_OPEN_READ:   fmode = "rb";  break;
  case MSPACK_SYS_OPEN_WRITE:  fmode = "wb";  break;
  case MSPACK_SYS_OPEN_UPDATE: fmode = "r+b"; break;
  case MSPACK_SYS_OPEN_APPEND: fmode = "ab";  break;
  default: return NULL;
  }

  if ((fh = malloc(sizeof(struct mspack_file_p)))) {
    fh->name = filename;

    if (filename == STDOUT_FNAME) {
      fh->regular_file = 0;
      fh->fh = stdout;
      return (struct mspack_file *) fh;
    }
    else if (filename == TEST_FNAME) {
      fh->regular_file = 0;
      fh->fh = NULL;
      md5_init_ctx(&md5_context);
      return (struct mspack_file *) fh;
    }
    else {
      /* regular file - simply attempt to open it */
      fh->regular_file = 1;
      if ((fh->fh = fopen(filename, fmode))) {
        return (struct mspack_file *) fh;
      }
    }
    /* error - free file handle and return NULL */
    free(fh);
  }
  return NULL;
}

static void cabx_close(struct mspack_file *file) {
  struct mspack_file_p *this = (struct mspack_file_p *) file;
  if (this) {
    if (this->name == TEST_FNAME) {
      md5_finish_ctx(&md5_context, (void *) &md5_result);
    }
    else if (this->regular_file) {
      fclose(this->fh);
    }
    free(this);
  }
}

static int cabx_read(struct mspack_file *file, void *buffer, int bytes) {
  struct mspack_file_p *this = (struct mspack_file_p *) file;
  if (this && this->regular_file && buffer && bytes >= 0) {
    size_t count = fread(buffer, 1, (size_t) bytes, this->fh);
    if (!ferror(this->fh)) return (int) count;
  }
  return -1;
}

static int cabx_write(struct mspack_file *file, void *buffer, int bytes) {
  struct mspack_file_p *this = (struct mspack_file_p *) file;
  if (this && buffer && bytes >= 0) {
    if (this->name == TEST_FNAME) {
      md5_process_bytes(buffer, (size_t) bytes, &md5_context);
      return bytes;
    }
    else {
      /* regular files and the stdout writer */
      size_t count = fwrite(buffer, 1, (size_t) bytes, this->fh);
      if (!ferror(this->fh)) return (int) count;
    }
  }
  return -1;
}

static int cabx_seek(struct mspack_file *file, off_t offset, int mode) {
  struct mspack_file_p *this = (struct mspack_file_p *) file;
  if (this && this->regular_file) {
    switch (mode) {
    case MSPACK_SYS_SEEK_START: mode = SEEK_SET; break;
    case MSPACK_SYS_SEEK_CUR:   mode = SEEK_CUR; break;
    case MSPACK_SYS_SEEK_END:   mode = SEEK_END; break;
    default: return -1;
    }
#if HAVE_FSEEKO
    return fseeko(this->fh, offset, mode);
#else
    return fseek(this->fh, offset, mode);
#endif
  }
  return -1;
}

static off_t cabx_tell(struct mspack_file *file) {
  struct mspack_file_p *this = (struct mspack_file_p *) file;
#if HAVE_FSEEKO
  return (this && this->regular_file) ? (off_t) ftello(this->fh) : 0;
#else
  return (this && this->regular_file) ? (off_t) ftell(this->fh) : 0;
#endif
}

static void cabx_msg(struct mspack_file *file, const char *format, ...) {
  va_list ap;
  if (file) {
    fprintf(stderr, "%s: ", ((struct mspack_file_p *) file)->name);
  }
  va_start(ap, format);
  vfprintf(stderr, format, ap);
  va_end(ap);
  fputc((int) '\n', stderr);
  fflush(stderr);
}
static void *cabx_alloc(struct mspack_system *this, size_t bytes) {
  return malloc(bytes);
}
static void cabx_free(void *buffer) {
  free(buffer);
}
static void cabx_copy(void *src, void *dest, size_t bytes) {
  memcpy(dest, src, bytes);
}
