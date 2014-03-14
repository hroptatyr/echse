include $(top_builddir)/version.mk

LANG = C
LC_ALL = C

ACLOCAL_AMFLAGS = -I m4

SUBDIRS =
SUBDIRS += build-aux
if DESCEND_LIBLTDL
SUBDIRS += libltdl
endif  DESCEND_LIBLTDL
SUBDIRS += src
SUBDIRS += beef

EXTRA_DIST =
DISTCLEANFILES =

EXTRA_DIST += git-version-gen
EXTRA_DIST += README.md

DISTCLEANFILES += version.mk

dist-hook:
	echo $(VERSION) > $(distdir)/version \
	&& touch -r $(top_builddir)/version.mk $(distdir)/version
