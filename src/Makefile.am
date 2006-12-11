##test
SUBDIRS = icons

gladedir = $(datadir)/quiver/glade
AM_CPPFLAGS = \
	-DPACKAGE_LOCALE_DIR=\""$(prefix)/$(DATADIRNAME)/locale"\" \
	-DPACKAGE_SRC_DIR=\""$(srcdir)"\" \
	-DPACKAGE_DATA_DIR=\""$(datadir)"\" \
	-DQUIVER_GLADEDIR=\""$(gladedir)"\" \
	$(PACKAGE_CFLAGS)\
	-Wall 
##-I$(top_srcdir)/libquiver \

bin_PROGRAMS = quiver

quiver_SOURCES = \
	quiver-i18n.h \
	QuiverUI.h \
	ExifView.h \
	ExifView.cpp \
	Preferences.h \
	Preferences.cpp \
	PreferencesDlg.h \
	PreferencesDlg.cpp \
	FolderTree.h \
	FolderTree.cpp \
	FolderTreeEventSource.h \
	FolderTreeEventSource.cpp \
	IFolderTreeEventHandler.h \
	FolderTreeEvent.h \
	ImageCache.h \
	ImageCache.cpp \
	ImageList.h \
	ImageList.cpp \
	ImageListEventSource.h \
	ImageListEventSource.cpp \
	IImageListEventHandler.h \
	ImageLoader.h \
	ImageLoader.cpp \
	ImageSaveManager.h \
	ImageSaveManager.cpp \
	QuiverFile.h \
	QuiverFile.cpp \
	QuiverFileOps.h \
	QuiverFileOps.cpp \
	IPixbufLoaderObserver.h \
	PixbufLoaderObserver.h \
	PixbufLoaderObserver.cpp \
	Timer.h \
	Timer.cpp \
	Browser.h \
	Browser.cpp \
	Viewer.h \
	Viewer.cpp \
	Statusbar.h \
	Statusbar.cpp \
	QuiverUtils.h \
	QuiverUtils.cpp \
	QuiverStockIcons.h \
	QuiverStockIcons.cpp \
	Quiver.h \
	Quiver.cpp \
	IEvent.h \
	EventBase.h \
	BrowserEvent.h \
	IEventSource.h \
	IEventHandler.h \
	IBrowserEventHandler.h \
	AbstractEventSource.h \
	AbstractEventSource.cpp \
	BrowserEventSource.h \
	BrowserEventSource.cpp \
	ViewerEvent.h \
	ViewerEventSource.h \
	ViewerEventSource.cpp \
	IViewerEventHandler.h 


quiver_LDFLAGS = 

quiver_LDADD = $(PACKAGE_LIBS) -ljpeg $(top_srcdir)/libquiver/libquiver.la /usr/lib/libboost_signals.a 


TESTS = test_image_list test_quiver_file test_icon_view
check_PROGRAMS = test_image_list test_quiver_file test_icon_view

test_image_list_SOURCES= test_image_list.cpp ImageList.cpp QuiverFile.cpp ImageCache.cpp Timer.cpp
test_image_list_LDADD = $(PACKAGE_LIBS)

test_quiver_file_SOURCES= test_quiver_file.cpp ImageCache.cpp QuiverFile.cpp Timer.cpp
test_quiver_file_LDADD = $(PACKAGE_LIBS)

test_icon_view_SOURCES= test_icon_view.cpp ImageCache.cpp QuiverFile.cpp Timer.cpp ImageList.cpp QuiverUtils.cpp
test_icon_view_LDADD = $(PACKAGE_LIBS)
