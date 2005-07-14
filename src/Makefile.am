## Process this file with automake to produce Makefile.in

## Created by Anjuta

AM_CPPFLAGS = \
	-DPACKAGE_LOCALE_DIR=\""$(prefix)/$(DATADIRNAME)/locale"\" \
	-DPACKAGE_SRC_DIR=\""$(srcdir)"\" \
	-DPACKAGE_DATA_DIR=\""$(datadir)"\" \
	$(PACKAGE_CFLAGS)\
	-Wall 

bin_PROGRAMS = quiver

quiver_SOURCES = \
	ImageCache.cpp \
	ImageList.cpp \
	ImageLoader.cpp \
	QuiverFile.cpp \
	NavigationControl.cpp \
	PixbufLoaderObserver.cpp \
	Timer.cpp \
	Viewer.cpp \
	Quiver.cpp

quiver_LDFLAGS = 

quiver_LDADD = $(PACKAGE_LIBS)


TESTS = test_image_list test_quiver_file test_icon_view
check_PROGRAMS = test_image_list test_quiver_file test_icon_view

test_image_list_SOURCES= test_image_list.cpp ImageList.cpp QuiverFile.cpp ImageCache.cpp Timer.cpp
test_image_list_LDADD = $(PACKAGE_LIBS)

test_quiver_file_SOURCES= test_quiver_file.cpp ImageCache.cpp QuiverFile.cpp Timer.cpp
test_quiver_file_LDADD = $(PACKAGE_LIBS)

test_icon_view_SOURCES= test_icon_view.cpp ImageCache.cpp QuiverFile.cpp Timer.cpp ImageList.cpp art_rgba_rgba_affine.c art_rgba_svp.c
test_icon_view_LDADD = $(PACKAGE_LIBS)
