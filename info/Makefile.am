# Help the Developers and yourself.  Just use the C locale and settings
# for the compilation. They can still be overriden by make LANG=<whatever>
# but that is general a not very good idea
LANG=C
LC_ALL=C

include $(top_builddir)/version.mk

BUILT_SOURCES =
EXTRA_DIST = $(BUILT_SOURCES)
EXTRA_DIST += $(man1_MANS)
SUFFIXES =

man1_MANS =
man1_MANS += $(built_mans)
man1_MANS += echse.man
man1_MANS += echsq.man
man1_MANS += echsd.man
man1_MANS += echsx.man

BUILT_SOURCES += $(built_mans)
EXTRA_DIST += author.h2m
EXTRA_DIST += proto

VPATH = $(srcdir):@top_srcdir@/src
SUFFIXES += .yuck
SUFFIXES += .man
.yuck.man:
	$(AM_V_GEN) PATH="$(top_builddir)/build-aux:$${PATH}" \
		yuck$(EXEEXT) genman \
			--package "$(PACKAGE_NAME)" \
			-i "$(srcdir)/author.h2m" \
			-o $@ --version-file "$(top_builddir)/.version" $<

## Makefile.am ends here
