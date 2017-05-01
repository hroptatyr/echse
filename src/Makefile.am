### Makefile.am
include $(top_builddir)/version.mk

AM_CFLAGS = $(EXTRA_CFLAGS)
AM_CPPFLAGS = -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 -D_BSD_SOURCE
AM_LDFLAGS = $(XCCLDFLAGS)

bin_PROGRAMS =
sbin_PROGRAMS =
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
EXTRA_libechse_la_SOURCES = $(libechse_la_built_sources)
libechse_la_SOURCES += instant.c instant.h
libechse_la_SOURCES += range.c range.h
libechse_la_SOURCES += dt-strpf.c dt-strpf.h
libechse_la_SOURCES += module.c module.h
libechse_la_SOURCES += hash.c hash.h
libechse_la_SOURCES += intern.c intern.h
libechse_la_SOURCES += state.c state.h
libechse_la_SOURCES += task.c task.h
libechse_la_SOURCES += strlst.c strlst.h
libechse_la_SOURCES += bufpool.c bufpool.h
libechse_la_SOURCES += event.c event.h
libechse_la_SOURCES += oid.h
libechse_la_SOURCES += instruc.h
libechse_la_SOURCES += fdprnt.h
libechse_la_SOURCES += nummapstr.h
libechse_la_SOURCES += evstrm.c evstrm.h
libechse_la_SOURCES += evical.c evical.h
libechse_la_SOURCES += evrrul.c evrrul.h
libechse_la_SOURCES += evmrul.c evmrul.h
libechse_la_SOURCES += evfilt.c evfilt.h
libechse_la_SOURCES += tzob.c tzob.h
libechse_la_SOURCES += scale.c scale.h
libechse_la_SOURCES += tzraw.c tzraw.h
libechse_la_SOURCES += boobs.h
libechse_la_SOURCES += bitint.c bitint.h
EXTRA_libechse_la_SOURCES += bitint-bobs.c
libechse_la_SOURCES += nifty.h
libechse_la_SOURCES += sock.h
libechse_la_SOURCES += echse-genuid.c echse-genuid.c
libechse_la_CPPFLAGS = $(AM_CPPFLAGS)
libechse_la_CPPFLAGS += $(LTDLINCL)
libechse_la_LDFLAGS = $(AM_LDFLAGS)
libechse_la_LDFLAGS += -lm
libechse_la_LDFLAGS += $(LIBLTDL) -export-dynamic
EXTRA_libechse_la_SOURCES += wikisort.c
EXTRA_libechse_la_DEPENDENCIES = $(LTDLDEPS)
BUILT_SOURCES += $(libechse_la_built_sources)
EXTRA_DIST += evical-gp.erf
EXTRA_DIST += evrrul-gp.erf
EXTRA_DIST += evmrul-gp.erf
EXTRA_DIST += evmeth-gp.erf
EXTRA_DIST += evcomp-gp.erf
libechse_la_built_sources =
libechse_la_built_sources += evical-gp.c
libechse_la_built_sources += evrrul-gp.c
libechse_la_built_sources += evmrul-gp.c
libechse_la_built_sources += evmeth-gp.c
libechse_la_built_sources += evcomp-gp.c

bin_PROGRAMS += echse
echse_SOURCES = echse.c echse.h echse.yuck
echse_SOURCES += version.h version.c
echse_SOURCES += echse-genuid.c echse-genuid.h
echse_CPPFLAGS = $(AM_CPPFLAGS) -DSTANDALONE
echse_CPPFLAGS += $(LTDLINCL)
echse_CPPFLAGS += -DHAVE_VERSION_H
echse_LDFLAGS = $(AM_LDFLAGS)
echse_LDFLAGS += $(LIBLTDL) -export-dynamic
echse_LDADD = libechse.la
EXTRA_echse_DEPENDENCIES = $(LTDLDEPS)
BUILT_SOURCES += echse.yucc

bin_PROGRAMS += echsq
echsq_SOURCES = echsq.c echsq.yuck
echsq_SOURCES += version.h version.c
echsq_CPPFLAGS = $(AM_CPPFLAGS) -DSTANDALONE
echsq_CPPFLAGS += -DHAVE_VERSION_H
echsq_LDADD = libechse.la
BUILT_SOURCES += echsq.yucc

if HAVE_LIBEV
if HAVE_RT_FUNS
bin_PROGRAMS += echsx
echsx_SOURCES = echsx.c echsx.yuck
echsx_SOURCES += version.h version.c
echsx_SOURCES += logger.c logger.h
echsx_SOURCES += nifty.h
echsx_CPPFLAGS = $(AM_CPPFLAGS)
echsx_CPPFLAGS += -DHAVE_VERSION_H
echsx_LDFLAGS = $(AM_LDFLAGS)
echsx_LDFLAGS += $(rt_LIBS)
echsx_CPPFLAGS += $(ev_CFLAGS)
echsx_LDFLAGS += $(ev_LIBS)
echsx_LDADD = libechse.la
BUILT_SOURCES += echsx.yucc
endif  HAVE_RT_FUNS

sbin_PROGRAMS += echsd
echsd_SOURCES = echsd.c echsd.yuck
echsd_SOURCES += logger.c logger.h
echsd_SOURCES += nifty.h
echsd_SOURCES += nedtrie.h
echsd_SOURCES += $(top_srcdir)/debian/echse.init
echsd_SOURCES += $(top_srcdir)/debian/echse.default
echsd_CPPFLAGS = $(AM_CPPFLAGS)
echsd_LDFLAGS = $(AM_LDFLAGS)
echsd_CPPFLAGS += $(ev_CFLAGS)
echsd_LDFLAGS += $(ev_LIBS)
echsd_LDADD = libechse.la
BUILT_SOURCES += echsd.yucc
endif  HAVE_LIBEV


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
