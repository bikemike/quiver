set(QUIVER_SOURCES
    MD5.cpp
    ExifView.cpp
    PreferencesEventSource.cpp
    Preferences.cpp
    PreferencesDlg.cpp
    FolderTree.cpp
    FolderTreeEventSource.cpp
    ImageCache.cpp
    ImageList.cpp
    ImageListEventSource.cpp
    ImageListFilter.cpp
    ImageLoader.cpp
    IconViewThumbLoader.cpp
    ImageSaveManager.cpp
    QuiverFile.cpp
    QuiverFileOps.cpp
    PixbufLoaderObserver.cpp
    Timer.cpp
    Browser.cpp
    Viewer.cpp
    Statusbar.cpp
    QuiverUtils.cpp
    QuiverStockIcons.cpp
    Quiver.cpp
    QuiverVideoOps.cpp
    AbstractEventSource.cpp
    BrowserEventSource.cpp
    ViewerEventSource.cpp
    AdjustDateDlg.cpp
    AdjustDateTask.cpp
    BookmarksEventSource.cpp
    BookmarksDlg.cpp
    BookmarkAddEditDlg.cpp
    Bookmarks.cpp
    BrowserHistory.cpp
    DonateDlg.cpp
    TaskEventSource.cpp
    TaskManagerDlg.cpp
    TaskManager.cpp
    TaskManagerEventSource.cpp
    AbstractTask.cpp
    OrganizeTask.cpp
    ThreadUtil.cpp
    MessageBox.cpp
    OrganizeDlg.cpp
    strnatcmp.c
    ExternalTools.cpp
    ExternalToolsDlg.cpp
    ExternalToolAddEditDlg.cpp
    ExternalToolsEventSource.cpp
    RenameDlg.cpp
    RenameTask.cpp
    FileConflictCheck.cpp
    SaveImageTask.cpp
    libjpeg/gio_jdatadst.c
    libjpeg/gio_jdatasrc.c
    libjpeg/transupp.c
)

add_executable(quiver ${QUIVER_SOURCES})

target_compile_definitions(quiver PRIVATE
    PACKAGE_LOCALE_DIR="${PACKAGE_LOCALE_DIR}"
    PACKAGE_SRC_DIR="${PACKAGE_SRC_DIR}"
    PACKAGE_DATA_DIR="${PACKAGE_DATA_DIR}"
    QUIVER_DATADIR="${QUIVER_DATADIR}"
)

target_include_directories(quiver PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(quiver PRIVATE
    quiver_lib
    PkgConfig::GTK3
    PkgConfig::GIO
    PkgConfig::LIBEXIF
    PkgConfig::LIBSQLITE
    PkgConfig::GSTREAMER
    PkgConfig::FFMPEG
    JPEG::JPEG
    Threads::Threads
    Boost::boost
)
