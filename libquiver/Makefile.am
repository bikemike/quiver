SUBDIRS = 
##icons cursors
DIST_SUBDIRS = 
##icons cursors

AM_CPPFLAGS =		\
	$(PACKAGE_CFLAGS) \
	-Wall

libquiverdir = $(libdir)/quiver
noinst_LTLIBRARIES = libquiver.la
##libquiver_LTLIBRARIES = libquiver.la
libquiver_la_LDFLAGS = -avoid-version

MARSHALLERS =              \
	quiver-marshallers.c   \
	quiver-marshallers.h	


libquiver_la_SOURCES =     \
	$(MARSHALLERS)         \
	quiver-pixbuf-utils.h  \
	quiver-pixbuf-utils.c  \
	quiver-image-view.h    \
	quiver-image-view.c    \
	quiver-icon-view.h      \
	quiver-icon-view.c      \
	quiver-navigation-control.h      \
	quiver-navigation-control.c



quiver-marshallers.h: quiver-marshallers.list $(GLIB_GENMARSHAL)
	$(GLIB_GENMARSHAL) quiver-marshallers.list --header --prefix=_quiver_marshal > $@

quiver-marshallers.c: quiver-marshallers.h quiver-marshallers.list $(GLIB_GENMARSHAL)
	echo "#include \"quiver-marshallers.h\"" > $@ && \
	$(GLIB_GENMARSHAL) quiver-marshallers.list --body --prefix=_quiver_marshal >> $@

CLEANFILES = $(MARSHALLERS) 
EXTRA_DIST = quiver-marshallers.list
