ACLOCAL_AMFLAGS = -I m4

AM_CFLAGS = \
	-fno-common \
	-fstack-protector \
	-std=gnu99 \
	-Wall \
	-Wformat \
	-Wformat-security \
	-Wimplicit-function-declaration \
	-Wno-conversion \
	-Wstrict-prototypes \
	-Wundef \
	-Wunreachable-code \
	-Wunused-variable

AM_CPPFLAGS = \
	$(AM_CFLAGS) \
	-D_FORTIFY_SOURCE=2 \
	-I$(top_srcdir)/include

EXTRA_DIST = \
	COPYING \
	findstatic.pl \
	README.chromium \
	src/bsdiff.sym

AUTOMAKE_OPTIONS = color-tests parallel-tests

if COVERAGE
AM_CFLAGS += --coverage

coverage: coverage-clean
	mkdir -p coverage
	lcov --compat-libtool --directory . --capture --output-file coverage/report
	genhtml -o coverage/ coverage/report

coverage-clean:
	rm -rf coverage
endif

bin_PROGRAMS = \
	bsdiff \
	bsdump \
	bspatch

bsdump_SOURCES = \
	src/dump_main.c

bsdump_LDADD = \
	libbsdiff.la

bsdiff_SOURCES = \
	src/diff_main.c

bsdiff_LDADD = \
	libbsdiff.la

bspatch_SOURCES = \
	src/patch_main.c

bspatch_LDADD = \
	libbsdiff.la

lib_LTLIBRARIES = \
	libbsdiff.la

libbsdiff_la_SOURCES = \
	src/diff.c \
	src/patch.c \
	src/sufsort.c

libbsdiff_la_LIBADD = \
	$(zlib_LIBS)

if ENABLE_LZMA
libbsdiff_la_LIBADD += \
	$(lzma_LIBS)
endif

pkgconfiglibdir=$(libdir)/pkgconfig
pkgconfiglib_DATA = \
	data/bsdiff.pc

include_HEADERS = \
	include/bsdiff.h

noinst_HEADERS = \
	src/bsheader.h

# Library version changes according to the libtool convention:
# http://www.gnu.org/software/libtool/manual/libtool.html#Updating-version-info
LIBBSDIFF_CURRENT=1
LIBBSDIFF_REVISION=0
LIBBSDIFF_AGE=0
libbsdiff_la_LDFLAGS = \
	-version-info $(LIBBSDIFF_CURRENT):$(LIBBSDIFF_REVISION):$(LIBBSDIFF_AGE) \
	-Wl,--version-script=$(top_srcdir)/src/bsdiff.sym

distclean-local:
	rm -rf aclocal.m4 ar-lib autom4te.cache config.guess config.h.in config.h.in~ config.sub configure depcomp install-sh ltmain.sh m4 Makefile.in missing compile

install-exec-hook:
	perl findstatic.pl */*.o | grep -v Checking ||:

TEST_EXTENSIONS = .sh

EXTRA_DIST += \
	test/data/5.bspatch.diff \
	test/data/5.bspatch.original \
	test/data/6.bspatch.diff \
	test/data/6.bspatch.original \
	test/data/7.bspatch.diff \
	test/data/7.bspatch.original \
	test/data/8.bspatch.diff \
	test/data/8.bspatch.original \
	test/data/9.bspatch.diff \
	test/data/9.bspatch.modified \
	test/data/9.bspatch.original \
	test/data/10.bspatch.diff \
	test/data/10.bspatch.modified \
	test/data/10.bspatch.original \
	test/data/11.bspatch.diff \
	test/data/12.bspatch.diff \
	test/data/12.bspatch.modified \
	test/data/12.bspatch.original \
	test/data/13.bspatch.modified \
	test/data/13.bspatch.original \
	test/data/14.bspatch.modified \
	test/data/14.bspatch.original \
	test/data/15.bspatch.modified \
	test/data/15.bspatch.original \
	test/data/16.bspatch.diff \
	test/data/16.bspatch.original

if ENABLE_TESTS
AM_TESTS_ENVIRONMENT = \
	abs_builddir=$(abs_builddir); export abs_builddir;

tap_driver = env AM_TAP_AWK='$(AWK)' $(SHELL) \
	$(top_srcdir)/tap-driver.sh

LOG_DRIVER = $(tap_driver)
SH_LOG_DRIVER = $(tap_driver)
TESTS = $(dist_check_SCRIPTS)
dist_check_SCRIPTS = \
	test/run.sh
endif

compliant:
	@git diff --quiet --exit-code include src; ret=$$?; \
	if [ $$ret -eq 1 ]; then \
		echo "Error: can only check code style when include/ and src/ are clean."; \
		echo "Stash or commit your changes and try again."; \
		exit $$ret; \
	elif [ $$ret -gt 1 ]; then \
		exit $$ret; \
	fi; \
	clang-format -i -style=file include/*.h src/*.c; ret=$$?; \
	if [ $$ret -ne 0 ]; then \
		exit $$ret; \
	fi; \
	git diff --quiet --exit-code include src; ret=$$?; \
	if [ $$ret -eq 1 ]; then \
		echo "Code style issues found. Run 'git diff' to view issues."; \
	elif [ $$ret -eq 0 ]; then \
		echo "No code style issues found."; \
	fi; \
	exit $$ret

release:
	@git rev-parse v$(PACKAGE_VERSION) &> /dev/null; \
	if [ "$$?" -eq 0 ]; then \
		echo "Error: Release $(PACKAGE_VERSION) already exists."; \
		echo "Bump version in configure.ac before releasing."; \
		exit 1; \
	fi
	@git tag -a -m "$(PACKAGE_NAME) release $(PACKAGE_VERSION)" v$(PACKAGE_VERSION)
	@printf "\nNew release $(PACKAGE_VERSION) tagged!\n\n"
