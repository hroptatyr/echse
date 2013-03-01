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


## predef streams
pkglib_LTLIBRARIES += xmas.la
xmas_la_LDFLAGS = $(MOD_LDFLAGS)

pkglib_LTLIBRARIES += easter.la
easter_la_LDFLAGS = $(MOD_LDFLAGS)

pkglib_LTLIBRARIES += new-year.la
new_year_la_LDFLAGS = $(MOD_LDFLAGS)

pkglib_LTLIBRARIES += weekend.la
weekend_la_LDFLAGS = $(MOD_LDFLAGS)

## predef filters
pkglib_LTLIBRARIES += collect.la
collect_la_SOURCES = collect.c
collect_la_SOURCES += gq.c gq.h
collect_la_LDFLAGS = $(MOD_LDFLAGS)
collect_la_LIBADD = libhattrie.la

## DSOs for internal use
pkglib_LTLIBRARIES += echs-file.la
echs_file_la_LDFLAGS = $(MOD_LDFLAGS)

pkglib_LTLIBRARIES += echs-filter.la
echs_filter_la_LDFLAGS = $(MOD_LDFLAGS)


noinst_PROGRAMS += echse
echse_SOURCES = echse.c echse.h
echse_SOURCES += echse-clo.ggo
echse_SOURCES += boobs.h
echse_SOURCES += instant.c instant.h
echse_SOURCES += dt-strpf.c dt-strpf.h
echse_SOURCES += strdef.c strdef.h
echse_SOURCES += fltdef.c fltdef.h
echse_SOURCES += module.c module.h
echse_CPPFLAGS = $(AM_CPPFLAGS) -DSTANDALONE
echse_CPPFLAGS += $(INCLTDL)
echse_LDFLAGS = $(AM_LDFLAGS)
echse_LDFLAGS += $(LIBLTDL) -export-dynamic
BUILT_SOURCES += echse-clo.c echse-clo.h

noinst_LTLIBRARIES += echse.la
echse_la_SOURCES = echse.c
echse_la_SOURCES += boobs.h
echse_la_SOURCES += instant.c instant.h
echse_la_SOURCES += dt-strpf.c dt-strpf.h
echse_la_SOURCES += strdef.c strdef.h
echse_la_SOURCES += module.c module.h
echse_la_LDFLAGS = $(MOD_LDFLAGS)
echse_la_LDFLAGS += -rpath /none

noinst_PROGRAMS += echse-collect
echse_collect_SOURCES = echse-collect.c
echse_collect_SOURCES += echse-collect-clo.ggo
echse_collect_SOURCES += echs-lisp.c echs-lisp.h
echse_collect_SOURCES += echs-lisp-keys.gperf
echse_collect_SOURCES += echs-lisp-syms.gperf
echse_collect_SOURCES += echs-lisp-funs.gperf
echse_collect_SOURCES += dt-strpf.c dt-strpf.h
echse_collect_SOURCES += module.c module.h
echse_collect_SOURCES += gq.c gq.h
echse_collect_CPPFLAGS = $(AM_CPPFLAGS)
echse_collect_CPPFLAGS += $(INCLTDL)
echse_collect_LDFLAGS = $(AM_LDFLAGS)
echse_collect_LDFLAGS += $(LIBLTDL) -export-dynamic
echse_collect_LDADD = libhattrie.la
BUILT_SOURCES += echse-collect-clo.c echse-collect-clo.h
BUILT_SOURCES += echs-lisp-keys.c
BUILT_SOURCES += echs-lisp-syms.c
BUILT_SOURCES += echs-lisp-funs.c

## daniel jones' hattrie shebang
noinst_LTLIBRARIES += libhattrie.la
libhattrie_la_SOURCES = hat-trie/hat-trie.c hat-trie/hat-trie.h
libhattrie_la_SOURCES += hat-trie/common.h
libhattrie_la_SOURCES += hat-trie/pstdint.h
libhattrie_la_SOURCES += hat-trie/ahtable.c hat-trie/ahtable.h
libhattrie_la_SOURCES += hat-trie/misc.c hat-trie/misc.h
libhattrie_la_SOURCES += hat-trie/murmurhash3.c hat-trie/murmurhash3.h
libhattrie_la_CPPFLAGS = $(AM_CPPFLAGS)
libhattrie_la_LDFLAGS = $(AM_LDFLAGS)


## gperf rule
%.c: %.gperf
	$(AM_V_GEN) $(GPERF) -L ANSI-C $< --output-file $@

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
