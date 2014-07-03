##test
SUBDIRS = icons

AM_CFLAGS = \
	-I$(top_srcdir) \
	-DPACKAGE_LOCALE_DIR=\""$(prefix)/$(DATADIRNAME)/locale"\" \
	-DPACKAGE_SRC_DIR=\""$(srcdir)"\" \
	-DPACKAGE_DATA_DIR=\""$(datadir)"\" \
	-DQUIVER_DATADIR=\""$(quiver_datadir)"\" \
	$(PACKAGE_CFLAGS) \
	$(HILDON_LIBS_CFLAGS) \
	$(HILDON_1_CFLAGS) \
	$(HILDON_FM_CFLAGS) \
	$(HILDON_FM_2_CFLAGS) \
	$(OSSO_MIME_CFLAGS) \
	$(HILDON_MIME_CFLAGS) \
	$(LIBEXIF_CFLAGS) \
	$(LIBSQLITE_CFLAGS) \
	$(GSTREAMER_CFLAGS) \
	-Wall 

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-DPACKAGE_LOCALE_DIR=\""$(prefix)/$(DATADIRNAME)/locale"\" \
	-DPACKAGE_SRC_DIR=\""$(srcdir)"\" \
	-DPACKAGE_DATA_DIR=\""$(datadir)"\" \
	-DQUIVER_DATADIR=\""$(quiver_datadir)"\" \
	$(PACKAGE_CFLAGS) \
	$(HILDON_LIBS_CFLAGS) \
	$(HILDON_1_CFLAGS) \
	$(HILDON_FM_CFLAGS) \
	$(HILDON_FM_2_CFLAGS) \
	$(OSSO_MIME_CFLAGS) \
	$(HILDON_MIME_CFLAGS) \
	$(BOOST_CPPFLAGS) \
	$(LIBEXIF_CFLAGS) \
	$(LIBSQLITE_CFLAGS) \
	$(GSTREAMER_CFLAGS) \
	-Wall 

bin_PROGRAMS = quiver 

quiver_SOURCES = \
	quiver-i18n.h \
	MD5.h \
	MD5.cpp \
	QuiverUI.h \
	ExifView.h \
	ExifView.cpp \
	Preferences.h \
	PreferencesEvent.h \
	IPreferencesEventHandler.h \
	PreferencesEventSource.h \
	PreferencesEventSource.cpp \
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
	ImageListEvent.h \
	ImageListEventSource.h \
	ImageListEventSource.cpp \
	IImageListEventHandler.h \
	ImageLoader.h \
	ImageLoader.cpp \
	IconViewThumbLoader.h \
	IconViewThumbLoader.cpp \
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
	QuiverPrefs.h \
	QuiverVideoOps.h \
	QuiverVideoOps.cpp \
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
	IViewerEventHandler.h \
	AdjustDateDlg.h \
	AdjustDateDlg.cpp \
	AdjustDateTask.h \
	AdjustDateTask.cpp \
	IViewerEventHandler.h \
	IBookmarksEventHandler.h \
	Bookmarks.h \
	BookmarksEvent.h \
	BookmarksEventSource.h \
	BookmarksEventSource.cpp \
	BookmarksDlg.h \
	BookmarksDlg.cpp \
	BookmarkAddEditDlg.h \
	BookmarkAddEditDlg.cpp \
	Bookmarks.cpp \
	BrowserHistory.h \
	BrowserHistory.cpp \
	DonateDlg.h \
	DonateDlg.cpp \
	TaskManager.h \
	TaskEventSource.cpp \
	TaskEvent.h \
	TaskManagerEvent.h \
	TaskManagerDlg.h \
	TaskManagerDlg.cpp \
	TaskManager.cpp \
	TaskManagerEventSource.cpp \
	ITaskEventHandler.h \
	ITaskManagerEventHandler.h \
	AbstractTask.h \
	AbstractTask.cpp \
	TaskManagerEventSource.h \
	TaskEventSource.h \
	OrganizeTask.h \
	OrganizeTask.cpp \
	ThreadUtil.h \
	ThreadUtil.cpp \
	MessageBox.h \
	MessageBox.cpp \
	OrganizeDlg.h \
	OrganizeDlg.cpp \
	strnatcmp.h \
	strnatcmp.c \
	ExternalTools.cpp \
	ExternalToolsDlg.cpp \
	ExternalToolsDlg.h \
	ExternalToolAddEditDlg.cpp \
	ExternalToolAddEditDlg.h \
	ExternalToolsEvent.h \
	ExternalToolsEventSource.cpp \
	ExternalToolsEventSource.h \
	ExternalTools.h \
	IExternalToolsEventHandler.h \
	RenameDlg.h \
	RenameDlg.cpp \
	RenameTask.h \
	RenameTask.cpp \
	SaveImageTask.h \
	SaveImageTask.cpp \
	libjpeg/jinclude.h \
	libjpeg/gio_jdatadst.c \
	libjpeg/gio_jdatasrc.c \
	libjpeg/transupp.h \
	libjpeg/transupp.c



quiver_LDFLAGS = 

quiver_LDADD = \
	../libquiver/libquiver.la \
	$(PACKAGE_LIBS) \
	$(HILDON_LIBS_LIBS) \
	$(HILDON_1_LIBS) \
	$(HILDON_FM_LIBS) \
	$(HILDON_FM_2_LIBS) \
	$(OSSO_MIME_LIBS) \
	$(HILDON_MIME_LIBS) \
	$(BOOST_LDFLAGS) \
	$(LIBEXIF_LIBS) \
	$(LIBSQLITE_LIBS) \
	$(GSTREAMER_LIBS) \
	-ljpeg

#TESTS = test_image_list test_quiver_file test_icon_view
#check_PROGRAMS = test_image_list test_quiver_file test_icon_view

#test_image_list_SOURCES= test_image_list.cpp ImageList.cpp QuiverFile.cpp ImageCache.cpp Timer.cpp
#test_image_list_LDADD = $(PACKAGE_LIBS)

#test_quiver_file_SOURCES= test_quiver_file.cpp ImageCache.cpp QuiverFile.cpp Timer.cpp
#test_quiver_file_LDADD = $(PACKAGE_LIBS)

#test_icon_view_SOURCES= test_icon_view.cpp ImageCache.cpp QuiverFile.cpp Timer.cpp ImageList.cpp QuiverUtils.cpp
#test_icon_view_LDADD = $(PACKAGE_LIBS)
