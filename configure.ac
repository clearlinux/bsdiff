AC_PREREQ([2.66])
AC_INIT([bsdiff], [1.0.0], [patrick.mccarty@intel.com])
AC_CONFIG_MACRO_DIR([m4])
AC_PROG_CC
AC_PROG_CC_STDC
AC_LANG(C)
AC_CONFIG_HEADERS([config.h])
AC_PREFIX_DEFAULT(/usr/local)
AC_CHECK_LIB([pthread], [pthread_create])

AM_INIT_AUTOMAKE([-Wall -Wno-portability no-dist-gzip dist-xz foreign subdir-objects])
AM_SILENT_RULES([yes])

LT_INIT

PKG_CHECK_MODULES([zlib], [zlib])
PKG_CHECK_MODULES([CHECK], [check >= 0.9])

AC_ARG_ENABLE([bzip2],
	      [AS_HELP_STRING([--disable-bzip2],[Do not use bzip2 compression (uses bzip2 by default)])])

AC_ARG_ENABLE([lzma],
	      [AS_HELP_STRING([--disable-lzma],[Do not use lzma compression (uses lzma by default)])])

AS_IF([test "$enable_bzip2" != "no"], [
  AC_CHECK_LIB([bz2], [BZ2_bzBuffToBuffCompress], [], [AC_MSG_ERROR([the libbz2 library is missing])])
  AC_DEFINE(BSDIFF_WITH_BZIP2,1,[Use bzip2 compression])
])

AS_IF([test "$enable_lzma" != "no"], [
  PKG_CHECK_MODULES([lzma], [liblzma])
  AC_DEFINE(BSDIFF_WITH_LZMA,1,[Use lzma compression])
])
AM_CONDITIONAL([ENABLE_LZMA], [test "$enable_lzma" != "no"])

have_coverage=no
AC_ARG_ENABLE(coverage, AS_HELP_STRING([--enable-coverage], [enable test coverage]))
if test "x$enable_coverage" = "xyes" ; then
        AC_CHECK_PROG(lcov_found, [lcov], [yes], [no])
        if test "x$lcov_found" = xno ; then
                AC_MSG_ERROR([*** lcov support requested but the program was not found])
        else
                lcov_version_major="`lcov --version | cut -d ' ' -f 4 | cut -d '.' -f 1`"
                lcov_version_minor="`lcov --version | cut -d ' ' -f 4 | cut -d '.' -f 2`"
                if test "$lcov_version_major" -eq 1 -a "$lcov_version_minor" -lt 10; then
                        AC_MSG_ERROR([*** lcov version is too old. 1.10 required])
                else
			have_coverage=yes
			AC_DEFINE([COVERAGE], [1], [Coverage enabled])
                fi
        fi
fi
AM_CONDITIONAL([COVERAGE], [test "$have_coverage" = "yes"])


AC_CONFIG_FILES([Makefile data/bsdiff.pc])
AC_OUTPUT

AC_MSG_RESULT([
        bsdiff $VERSION
        ========

        prefix:                 ${prefix}
        libdir:                 ${libdir}
        exec_prefix:            ${exec_prefix}
        bindir:                 ${bindir}

        compiler:               ${CC}
        cflags:                 ${CFLAGS}
        ldflags:                ${LDFLAGS}
])
