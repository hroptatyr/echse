### Makefile.am
include $(top_builddir)/version.mk

AM_CFLAGS = $(EXTRA_CFLAGS)
AM_CPPFLAGS = -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 -D_BSD_SOURCE
AM_CPPFLAGS += -DHAVE_VERSION_H
AM_LDFLAGS = $(XCCLDFLAGS)
MOD_LDFLAGS = $(XCCLDFLAGS) -shared -avoid-version -module

bin_PROGRAMS =
noinst_PROGRAMS =
lib_LTLIBRARIES =
noinst_LTLIBRARIES =
pkglib_LTLIBRARIES =
noinst_HEADERS =
BUILT_SOURCES =
EXTRA_DIST = $(BUILT_SOURCES)
CLEANFILES = 
SUFFIXES =

CLEANFILES += version.c
EXTRA_DIST += version.c.in
noinst_HEADERS += version.h


## predef streams
pkglib_LTLIBRARIES += xmas.la
xmas_la_LDFLAGS = $(MOD_LDFLAGS)

pkglib_LTLIBRARIES += xmas2.la
xmas2_la_LDFLAGS = $(MOD_LDFLAGS)

pkglib_LTLIBRARIES += easter.la
easter_la_LDFLAGS = $(MOD_LDFLAGS)

pkglib_LTLIBRARIES += new-year.la
new_year_la_LDFLAGS = $(MOD_LDFLAGS)

pkglib_LTLIBRARIES += weekend.la
weekend_la_LDFLAGS = $(MOD_LDFLAGS)

pkglib_LTLIBRARIES += solar.la
solar_la_SOURCES = solar.c
solar_la_SOURCES += celest.c celest.h
solar_la_SOURCES += yd.h
solar_la_LDFLAGS = $(MOD_LDFLAGS)

pkglib_LTLIBRARIES += lunar.la
lunar_la_SOURCES = lunar.c
lunar_la_SOURCES += celest.c celest.h
lunar_la_SOURCES += yd.h
lunar_la_LDFLAGS = $(MOD_LDFLAGS)

pkglib_LTLIBRARIES += zoneinfo.la
zoneinfo_la_SOURCES = zoneinfo.c
zoneinfo_la_SOURCES += boobs.h
zoneinfo_la_SOURCES += yd.h
zoneinfo_la_LDFLAGS = $(MOD_LDFLAGS)

## predef filters
pkglib_LTLIBRARIES += collect.la
collect_la_SOURCES = collect.c
collect_la_SOURCES += gq.c gq.h
collect_la_LDFLAGS = $(MOD_LDFLAGS)
collect_la_LIBADD = libhattrie.la

pkglib_LTLIBRARIES += in-lieu.la
in_lieu_la_SOURCES = in-lieu.c
in_lieu_la_SOURCES += gq.c gq.h
in_lieu_la_LDFLAGS = $(MOD_LDFLAGS)
in_lieu_la_LIBADD = libhattrie.la

## DSOs for internal use
pkglib_LTLIBRARIES += echs-file.la
echs_file_la_LDFLAGS = $(MOD_LDFLAGS)

pkglib_LTLIBRARIES += echs-filter.la
echs_filter_la_LDFLAGS = $(MOD_LDFLAGS)

## version lib
noinst_LTLIBRARIES += libechsv.la
libechsv_la_SOURCES = version.c version.h
libechsv_la_LDFLAGS = -static


## ourself as .so
noinst_LTLIBRARIES += libechse.la
libechse_la_SOURCES =
libechse_la_SOURCES += instant.c instant.h
libechse_la_SOURCES += dt-strpf.c dt-strpf.h
libechse_la_SOURCES += strdef.c strdef.h
libechse_la_SOURCES += fltdef.c fltdef.h
libechse_la_SOURCES += module.c module.h
libechse_la_SOURCES += builders.c builders.h
libechse_la_SOURCES += strctl.c strctl.h
libechse_la_SOURCES += event.h
libechse_la_CPPFLAGS = $(AM_CPPFLAGS)
libechse_la_CPPFLAGS += $(LTDLINCL)
libechse_la_LDFLAGS = $(AM_LDFLAGS)
libechse_la_LDFLAGS += $(LIBLTDL) -export-dynamic
EXTRA_libechse_la_DEPENDENCIES = $(LTDLDEPS)


noinst_PROGRAMS += echse
echse_SOURCES = echse.c echse.h echse.yuck
echse_SOURCES += boobs.h
echse_SOURCES += $(libechse_la_SOURCES)
echse_CPPFLAGS = $(AM_CPPFLAGS) -DSTANDALONE
echse_CPPFLAGS += $(LTDLINCL)
echse_LDFLAGS = $(AM_LDFLAGS)
echse_LDFLAGS += $(LIBLTDL) -export-dynamic
echse_LDADD = libechsv.la
EXTRA_echse_DEPENDENCIES = $(LTDLDEPS)
BUILT_SOURCES += echse.yucc

noinst_LTLIBRARIES += echse.la
echse_la_SOURCES = echse.c
echse_la_SOURCES += boobs.h
echse_la_SOURCES += $(libechse_la_SOURCES)
echse_la_LDFLAGS = $(MOD_LDFLAGS)
echse_la_LDFLAGS += -rpath /none

## transform edge files into states
noinst_PROGRAMS += echse-state
echse_state_SOURCES = echse-state.c echse.yuck
echse_state_SOURCES += boobs.h
echse_state_SOURCES += $(libechse_la_SOURCES)
echse_state_CPPFLAGS = $(AM_CPPFLAGS) -DSTANDALONE
echse_state_CPPFLAGS += $(LTDLINCL)
echse_state_LDFLAGS = $(AM_LDFLAGS)
echse_state_LDFLAGS += $(LIBLTDL) -export-dynamic
echse_state_LDADD = libechsv.la
EXTRA_echse_state_DEPENDENCIES = $(LTDLDEPS)
BUILT_SOURCES += echse.yucc


## version rules
version.c: version.c.in $(top_builddir)/.version
	$(AM_V_GEN) PATH="$(top_builddir)/build-aux:$${PATH}" \
		yuck$(EXEEXT) scmver --ignore-noscm --force -o $@ \
			--use-reference --reference $(top_builddir)/.version $<

## gperf rule
SUFFIXES += .gperf
.gperf.c:
	$(AM_V_GEN) $(GPERF) -L ANSI-C "$<" --output-file "$@"

## yuck rule
SUFFIXES += .yuck
SUFFIXES += .yucc
.yuck.yucc:
	$(AM_V_GEN) PATH=$(top_builddir)/build-aux:"$${PATH}" \
		yuck$(EXEEXT) gen -o $@ $<

## Create preprocessor output (debugging purposes only)
.c.i:
	$(COMPILE) -E -o $@ $<

## Create assembler output (debugging purposes only)
.c.s:
	$(COMPILE) -S -c $(AM_CFLAGS) $<
