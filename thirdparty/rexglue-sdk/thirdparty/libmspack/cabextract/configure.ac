# -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.
AC_PREREQ([2.69])
AC_INIT([cabextract],[1.11],[kyzer@cabextract.org.uk])
AM_INIT_AUTOMAKE
AC_CONFIG_SRCDIR([src/cabextract.c])
AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CC
AM_PROG_AR
AC_PROG_INSTALL
AC_PROG_RANLIB

# Checks for header files.
AC_CHECK_HEADERS([getopt.h inttypes.h strings.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_C_INLINE
AC_C_BIGENDIAN
AC_TYPE_MODE_T
AC_TYPE_OFF_T
AC_TYPE_SIZE_T
AC_SYS_LARGEFILE
AC_CHECK_SIZEOF([off_t])

# Checks for library functions.
AC_FUNC_FNMATCH
AC_FUNC_FSEEKO
AX_FUNC_MKDIR
AC_CHECK_FUNCS([towlower umask utime utimes])
AC_CHECK_FUNCS([getopt_long],,[AC_CHECK_LIB([gnugetopt], [getopt_long],
  [AC_DEFINE([HAVE_GETOPT_LONG])],[AC_LIBOBJ(getopt) AC_LIBOBJ(getopt1)])])
AC_REPLACE_FNMATCH
AM_ICONV

# use an external libmspack if requested
cabextract_external_libmspack=no
AC_ARG_WITH([external-libmspack],
 [AS_HELP_STRING([--with-external-libmspack],[use external libmspack (default no)])],
 [cabextract_external_libmspack=$withval])

if test "z$cabextract_external_libmspack" != 'zno'; then
  PKG_CHECK_MODULES([libmspack], [libmspack >= 0.8alpha])
  AC_CHECK_HEADER([mspack.h], ,[AC_MSG_ERROR([Cannot find libmspack header])])
  AC_CHECK_LIB([mspack],[mspack_create_cab_decompressor], ,
   [AC_MSG_ERROR([Cannot find libmspack library])])
fi
AM_CONDITIONAL([EXTERNAL_LIBMSPACK], [test "z$cabextract_external_libmspack" != 'zno'])

AC_CONFIG_FILES([Makefile cabextract.spec test/testcase])
AC_OUTPUT
