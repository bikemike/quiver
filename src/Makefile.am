##test
SUBDIRS = icons

AM_CPPFLAGS = \
	-DPACKAGE_LOCALE_DIR=\""$(prefix)/$(DATADIRNAME)/locale"\" \
	-DPACKAGE_SRC_DIR=\""$(srcdir)"\" \
	-DPACKAGE_DATA_DIR=\""$(datadir)"\" \
	$(PACKAGE_CFLAGS)\
	-Wall 

bin_PROGRAMS = quiver

quiver_SOURCES = \
	quiver-i18n.h \
	QuiverUI.h \
	ImageCache.h \
	ImageCache.cpp \
	ImageList.h \
	ImageList.cpp \
	ImageLoader.h \
	ImageLoader.cpp \
	QuiverFile.h \
	QuiverFile.cpp \
	NavigationControl.h \
	NavigationControl.cpp \
	PixbufLoaderObserver.h \
	PixbufLoaderObserver.cpp \
	Timer.h \
	Timer.cpp \
	Viewer.h \
	Viewer.cpp \
	Statusbar.h \
	Statusbar.cpp \
	QuiverStockIcons.h \
	QuiverStockIcons.cpp \
	Quiver.h \
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
