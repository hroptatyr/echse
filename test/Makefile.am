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
TESTS += evical_prnt_01.clit

check_PROGRAMS += bitint_test_01
bitint_test_01_CPPFLAGS = $(AM_CPPFLAGS)
bitint_test_01_CPPFLAGS += $(echse_CFLAGS)
TESTS += bitint_test_01.clit

check_PROGRAMS += bitint_test_02
bitint_test_02_CPPFLAGS = $(AM_CPPFLAGS)
bitint_test_02_CPPFLAGS += $(echse_CFLAGS)
TESTS += bitint_test_02.clit

check_PROGRAMS += bitint_test_03
bitint_test_03_CPPFLAGS = $(AM_CPPFLAGS)
bitint_test_03_CPPFLAGS += $(echse_CFLAGS)
TESTS += bitint_test_03.clit

check_PROGRAMS += bitint_test_04
bitint_test_04_CPPFLAGS = $(AM_CPPFLAGS)
bitint_test_04_CPPFLAGS += $(echse_CFLAGS)
TESTS += bitint_test_04.clit

check_PROGRAMS += bitint_test_05
bitint_test_05_CPPFLAGS = $(AM_CPPFLAGS)
bitint_test_05_CPPFLAGS += $(echse_CFLAGS)
TESTS += bitint_test_05.clit

check_PROGRAMS += bitint_test_06
bitint_test_06_CPPFLAGS = $(AM_CPPFLAGS)
bitint_test_06_CPPFLAGS += $(echse_CFLAGS)
TESTS += bitint_test_06.clit

check_PROGRAMS += bitint_test_07
bitint_test_07_CPPFLAGS = $(AM_CPPFLAGS)
bitint_test_07_CPPFLAGS += $(echse_CFLAGS)
TESTS += bitint_test_07.clit

check_PROGRAMS += bitint_test_08
bitint_test_08_CPPFLAGS = $(AM_CPPFLAGS)
bitint_test_08_CPPFLAGS += $(echse_CFLAGS)
TESTS += bitint_test_08.clit

check_PROGRAMS += bitint_test_09
bitint_test_09_CPPFLAGS = $(AM_CPPFLAGS)
bitint_test_09_CPPFLAGS += $(echse_CFLAGS)
bitint_test_09_LDFLAGS = $(echse_LIBS)
TESTS += bitint_test_09.clit

check_PROGRAMS += bitint_test_10
bitint_test_10_CPPFLAGS = $(AM_CPPFLAGS)
bitint_test_10_CPPFLAGS += $(echse_CFLAGS)
bitint_test_10_LDFLAGS = $(echse_LIBS)
TESTS += bitint_test_10.clit

check_PROGRAMS += bitint_test_11
bitint_test_11_CPPFLAGS = $(AM_CPPFLAGS)
bitint_test_11_CPPFLAGS += $(echse_CFLAGS)
bitint_test_11_LDFLAGS = $(echse_LIBS)
TESTS += bitint_test_11.clit

check_PROGRAMS += bitint_test_12
bitint_test_12_CPPFLAGS = $(AM_CPPFLAGS)
bitint_test_12_CPPFLAGS += $(echse_CFLAGS)
bitint_test_12_LDFLAGS = $(echse_LIBS)
TESTS += bitint_test_12.clit

EXTRA_DIST += sample_01.ics
EXTRA_DIST += sample_02.ics
EXTRA_DIST += sample_03.ics
EXTRA_DIST += sample_04.ics
EXTRA_DIST += sample_05.ics
EXTRA_DIST += sample_06.ics
EXTRA_DIST += sample_07.ics
EXTRA_DIST += sample_08.ics
EXTRA_DIST += sample_09.ics
EXTRA_DIST += sample_10.ics
EXTRA_DIST += sample_11.ics
EXTRA_DIST += sample_12.ics
EXTRA_DIST += sample_13.ics
EXTRA_DIST += sample_14.ics
EXTRA_DIST += sample_15.ics
EXTRA_DIST += sample_16.ics
EXTRA_DIST += sample_17.ics
EXTRA_DIST += sample_18.ics
EXTRA_DIST += sample_19.ics
EXTRA_DIST += sample_20.ics
EXTRA_DIST += sample_21.ics

TESTS += rrul_01.clit
TESTS += rrul_02.clit
TESTS += rrul_03.clit
TESTS += rrul_04.clit
TESTS += rrul_05.clit
TESTS += rrul_06.clit
TESTS += rrul_07.clit
TESTS += rrul_08.clit
TESTS += rrul_09.clit
TESTS += rrul_10.clit
TESTS += rrul_11.clit
TESTS += rrul_12.clit
TESTS += rrul_13.clit
TESTS += rrul_14.clit
TESTS += rrul_15.clit
TESTS += rrul_16.clit
TESTS += rrul_17.clit
TESTS += rrul_18.clit
TESTS += rrul_19.clit
TESTS += rrul_20.clit
TESTS += rrul_21.clit
TESTS += rrul_22.clit
TESTS += rrul_23.clit
TESTS += rrul_24.clit
TESTS += rrul_25.clit
TESTS += rrul_26.clit
TESTS += rrul_27.clit

TESTS += genuid_01.clit
TESTS += genuid_02.clit

TESTS += merge_01.clit
TESTS += merge_02.clit

TESTS += filt_01.clit
TESTS += filt_02.clit
TESTS += filt_03.clit
TESTS += filt_04.clit

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
