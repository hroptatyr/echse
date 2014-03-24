# Help the Developers and yourself. Just use the C locale and settings
# for the compilation. They can still be overriden by make LANG=<whatever>
# but that is general a not very good idea
include $(top_builddir)/version.mk

LANG = C
LC_ALL = C

AM_CFLAGS = $(EXTRA_CFLAGS)
AM_CPPFLAGS = -D_POSIX_C_SOURCE=200112L -D_XOPEN_SOURCE=600 -D_BSD_SOURCE
AM_CPPFLAGS += -DTEST
echse_CFLAGS = -I$(top_srcdir)/src
echse_LIBS = $(top_builddir)/src/libechse.la

EXTRA_DIST = $(BUILT_SOURCES) $(TESTS)
TESTS =
TEST_EXTENSIONS =
BUILT_SOURCES =
check_PROGRAMS =
CLEANFILES = $(check_PROGRAMS)
noinst_LIBRARIES =

TEST_EXTENSIONS += .clit
TESTS_ENVIRONMENT = root=$(top_srcdir)
CLIT_LOG_COMPILER = $(builddir)/clitoris
AM_CLIT_LOG_FLAGS = -v --builddir $(top_builddir)/src
AM_LOG_COMPILER = false

## actual regression tests
check_PROGRAMS += evical_prnt
evical_prnt_CPPFLAGS = $(AM_CPPFLAGS)
evical_prnt_CPPFLAGS += $(echse_CFLAGS)
evical_prnt_LDADD = $(echse_LIBS)

## our friendly helpers
check_PROGRAMS += clitoris
clitoris_CPPFLAGS = $(AM_CPPFLAGS)
clitoris_CPPFLAGS += -D_GNU_SOURCE
clitoris_LDFLAGS = $(AM_LDFLAGS) -static
clitoris_LDADD = -lutil
BUILT_SOURCES += clitoris.yucc


## yuck rule
SUFFIXES = .yucc .yuck
.yuck.yucc:
	$(AM_V_GEN) PATH=$(top_builddir)/build-aux:"$${PATH}" \
		yuck$(EXEEXT) gen -o $@ $<

## Makefile.am ends here
