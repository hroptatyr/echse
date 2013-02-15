### Makefile.am

AM_CPPFLAGS = -D_POSIX_C_SOURCE=200112L -D_XOPEN_SOURCE=600 -D_BSD_SOURCE
AM_LDFLAGS =
MOD_LDFLAGS = -shared -avoid-version -module

bin_PROGRAMS =
noinst_PROGRAMS =
lib_LTLIBRARIES =
noinst_LTLIBRARIES =
pkglib_LTLIBRARIES =
noinst_HEADERS =
BUILT_SOURCES =
EXTRA_DIST =
CLEANFILES = $(BUILT_SOURCES)


pkglib_LTLIBRARIES += xmas.la
xmas_la_LDFLAGS = $(MOD_LDFLAGS)

pkglib_LTLIBRARIES += easter.la
easter_la_LDFLAGS = $(MOD_LDFLAGS)


noinst_PROGRAMS += echse
echse_SOURCES = echse.c echse.h
echse_SOURCES += boobs.h
echse_SOURCES += dt-strpf.c dt-strpf.h


## our rule for gengetopt
%.c %.h: %.ggo
	$(AM_V_GEN) $(GENGETOPT) -l -i $< -F $*

# 
# Help the developers get nice post-processed source files

## Create preprocessor output (debugging purposes only)
.c.i:
	$(COMPILE) -E -o $@ $<

## Create assembler output (debugging purposes only)
.c.s:
	$(COMPILE) -S -c $(AM_CFLAGS) $<
