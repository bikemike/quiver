set(QUIVER_CORE_SOURCES
    MD5.cpp
    PropertyView.cpp
    VideoDateEditTask.cpp
    PreferencesEventSource.cpp
    Preferences.cpp
    PreferencesDlg.cpp
    FolderTree.cpp
    FolderTreeEventSource.cpp
    ImageCache.cpp
    ImageList.cpp
    ImageListEventSource.cpp
    ImageListFilter.cpp
    ImageDecoder.cpp
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
    ShortcutManager.cpp
    QuiverStockIcons.cpp
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

add_library(quiver_core STATIC ${QUIVER_CORE_SOURCES})

target_compile_definitions(quiver_core PUBLIC
    PACKAGE_LOCALE_DIR="${PACKAGE_LOCALE_DIR}"
    PACKAGE_SRC_DIR="${PACKAGE_SRC_DIR}"
    PACKAGE_DATA_DIR="${PACKAGE_DATA_DIR}"
    QUIVER_DATADIR="${QUIVER_DATADIR}"
)

target_include_directories(quiver_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_BINARY_DIR}
)

target_link_libraries(quiver_core PUBLIC
    quiver_lib
    PkgConfig::GTK4
    PkgConfig::GIO
    PkgConfig::EXIV2
    PkgConfig::LIBSQLITE
    PkgConfig::GSTREAMER
    PkgConfig::FFMPEG
    JPEG::JPEG
    Threads::Threads
    Boost::boost
)

if (TARGET PkgConfig::GLYCIN)
    target_link_libraries(quiver_core PUBLIC PkgConfig::GLYCIN)
endif()

add_executable(quiver Quiver.cpp)
target_link_libraries(quiver PRIVATE quiver_core)
