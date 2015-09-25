# Help the Developers and yourself. Just use the C locale and settings
# for the compilation. They can still be overriden by make LANG=<whatever>
# but that is general a not very good idea
include $(top_builddir)/version.mk

LANG = C
LC_ALL = C

AM_CFLAGS = $(EXTRA_CFLAGS)
AM_CPPFLAGS = -D_POSIX_C_SOURCE=200112L -D_XOPEN_SOURCE=600 -D_BSD_SOURCE
AM_CPPFLAGS += -DTEST
AM_CPPFLAGS += -UHAVE_CONFIG_H -UHAVE_VERSION_H
echse_CFLAGS = -I$(top_srcdir)/src
echse_LIBS = $(top_builddir)/src/libechse.la

EXTRA_DIST = $(BUILT_SOURCES) $(TESTS)
TESTS =
NOTESTS =
TEST_EXTENSIONS =
BUILT_SOURCES =
check_PROGRAMS =
CLEANFILES = $(check_PROGRAMS)
noinst_LIBRARIES =

## summon our helper
include clitoris.am
AM_CLIT_LOG_FLAGS = -v --builddir $(top_builddir)/src
TESTS_ENVIRONMENT = root=$(top_srcdir)
TESTS_ENVIRONMENT += TZDIR=$(TZDIR) TZDIR_RIGHT=$(TZDIR_RIGHT)

## actual regression tests
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
EXTRA_DIST += sample_12a.ics
EXTRA_DIST += sample_12b.ics
EXTRA_DIST += sample_13.ics
EXTRA_DIST += sample_13a.ics
EXTRA_DIST += sample_14.ics
EXTRA_DIST += sample_15.ics
EXTRA_DIST += sample_16.ics
EXTRA_DIST += sample_17.ics
EXTRA_DIST += sample_18.ics
EXTRA_DIST += sample_19.ics
EXTRA_DIST += sample_20.ics
EXTRA_DIST += sample_21.ics
EXTRA_DIST += sample_22.ics
EXTRA_DIST += sample_23.ics
EXTRA_DIST += sample_24.ics
EXTRA_DIST += sample_25.ics
EXTRA_DIST += sample_26.ics
EXTRA_DIST += sample_27.ics
EXTRA_DIST += sample_28.ics
EXTRA_DIST += sample_29.ics
EXTRA_DIST += sample_30.ics
EXTRA_DIST += sample_31.ics
EXTRA_DIST += sample_32.ics
EXTRA_DIST += sample_33.ics
EXTRA_DIST += sample_34.ics

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
TESTS += rrul_28.clit
TESTS += rrul_29.clit
TESTS += rrul_30.clit
TESTS += rrul_31.clit
TESTS += rrul_32.clit
TESTS += rrul_33.clit
TESTS += rrul_34.clit
TESTS += rrul_35.clit
TESTS += rrul_36.clit
TESTS += rrul_37.clit
TESTS += rrul_38.clit
TESTS += rrul_39.clit
TESTS += rrul_40.clit
TESTS += rrul_41.clit
TESTS += rrul_42.clit

TESTS += genuid_01.clit
TESTS += genuid_02.clit

TESTS += merge_01.clit
TESTS += merge_02.clit
TESTS += merge_03.clit
TESTS += merge_04.clit

TESTS += filt_01.clit
TESTS += filt_02.clit
TESTS += filt_03.clit
TESTS += filt_04.clit

NOTESTS += mrul_01.clit
NOTESTS += mrul_02.clit
NOTESTS += mrul_03.clit
NOTESTS += mrul_04.clit


## Makefile.am ends here
