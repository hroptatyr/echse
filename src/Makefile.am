### Makefile.am

AM_CPPFLAGS = -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 -D_BSD_SOURCE
AM_LDFLAGS = $(XCCLDFLAGS)
MOD_LDFLAGS = $(XCCLDFLAGS) -shared -avoid-version -module

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

pkglib_LTLIBRARIES += new-year.la
new_year_la_LDFLAGS = $(MOD_LDFLAGS)

pkglib_LTLIBRARIES += weekend.la
weekend_la_LDFLAGS = $(MOD_LDFLAGS)

## DSOs for internal use
pkglib_LTLIBRARIES += echs-file.la
echs_file_la_LDFLAGS = $(MOD_LDFLAGS)


noinst_PROGRAMS += echse
echse_SOURCES = echse.c echse.h
echse_SOURCES += echse-clo.ggo
echse_SOURCES += boobs.h
echse_SOURCES += instant.c instant.h
echse_SOURCES += dt-strpf.c dt-strpf.h
echse_SOURCES += stream.c stream.h
echse_SOURCES += module.c module.h
echse_CPPFLAGS = $(AM_CPPFLAGS)
echse_CPPFLAGS += $(INCLTDL)
echse_LDFLAGS = $(AM_LDFLAGS)
echse_LDFLAGS += $(LIBLTDL) -export-dynamic
BUILT_SOURCES += echse-clo.c echse-clo.h


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
