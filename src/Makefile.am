### Makefile.am
include $(top_builddir)/version.mk

AM_CFLAGS = $(EXTRA_CFLAGS)
AM_CPPFLAGS = -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 -D_BSD_SOURCE
AM_LDFLAGS = $(XCCLDFLAGS)

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


## ourself as .so
noinst_LTLIBRARIES += libechse.la
libechse_la_SOURCES = echse.h
libechse_la_SOURCES += instant.c instant.h
libechse_la_SOURCES += dt-strpf.c dt-strpf.h
libechse_la_SOURCES += module.c module.h
libechse_la_SOURCES += intern.c intern.h
libechse_la_SOURCES += bufpool.c bufpool.h
libechse_la_SOURCES += event.c event.h
libechse_la_SOURCES += evstrm.c evstrm.h
libechse_la_SOURCES += evical.c evical.h
libechse_la_SOURCES += evrrul.c evrrul.h
libechse_la_SOURCES += boobs.h
libechse_la_SOURCES += bitint.c bitint.h
libechse_la_SOURCES += nifty.h
## 3rd party blake2
libechse_la_SOURCES += blake2/blake2b.c blake2/blake2.h blake2/blake2-impl.h
libechse_la_SOURCES += blake2/blake2-config.h
libechse_la_SOURCES += blake2/blake2b-load-sse2.h blake2/blake2b-load-sse41.h
libechse_la_CPPFLAGS = $(AM_CPPFLAGS)
libechse_la_CPPFLAGS += $(LTDLINCL)
libechse_la_LDFLAGS = $(AM_LDFLAGS)
libechse_la_LDFLAGS += -lm
libechse_la_LDFLAGS += $(LIBLTDL) -export-dynamic
EXTRA_libechse_la_SOURCES = $(libechse_la_built_sources)
EXTRA_libechse_la_SOURCES += wikisort.c
EXTRA_libechse_la_DEPENDENCIES = $(LTDLDEPS)
BUILT_SOURCES += $(libechse_la_built_sources)
libechse_la_built_sources =
libechse_la_built_sources += evical-gp.c
libechse_la_built_sources += evrrul-gp.c

bin_PROGRAMS += echse
echse_SOURCES = echse.c echse.h echse.yuck
echse_SOURCES += version.h version.c
echse_CPPFLAGS = $(AM_CPPFLAGS) -DSTANDALONE
echse_CPPFLAGS += $(LTDLINCL)
echse_CPPFLAGS += -DHAVE_VERSION_H
echse_LDFLAGS = $(AM_LDFLAGS)
echse_LDFLAGS += $(LIBLTDL) -export-dynamic
echse_LDADD = libechse.la
EXTRA_echse_DEPENDENCIES = $(LTDLDEPS)
BUILT_SOURCES += echse.yucc


## version rules
version.c: version.c.in $(top_builddir)/.version
	$(AM_V_GEN) PATH="$(top_builddir)/build-aux:$${PATH}" \
		yuck$(EXEEXT) scmver --ignore-noscm --force -o $@ \
			--use-reference --reference $(top_builddir)/.version $<

## gperf rule
SUFFIXES += .erf
.erf.c:
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
