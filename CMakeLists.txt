cmake_minimum_required(VERSION 3.10)
if (POLICY CMP0167)
  cmake_policy(SET CMP0167 OLD)
endif()
project(quiver VERSION 0.3.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_compile_options(-Wall -Wextra)

# AddressSanitizer and UndefinedBehaviorSanitizer option
option(ENABLE_ASAN "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
if (ENABLE_ASAN)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

# Find pkg-config
find_package(PkgConfig REQUIRED)

# Image decoding backend options
option(ENABLE_GLYCIN "Enable libglycin backend for image loading" OFF)
option(ENABLE_GDK_PIXBUF "Enable GdkPixbuf loader backend for image loading" ON)

# Find Packages
pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4>=4.12)
pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0)
pkg_check_modules(EXIV2 REQUIRED IMPORTED_TARGET exiv2)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil libswscale)
pkg_check_modules(LIBSQLITE REQUIRED IMPORTED_TARGET sqlite3)
pkg_check_modules(GSTREAMER REQUIRED IMPORTED_TARGET gstreamer-1.0 gstreamer-plugins-base-1.0 gstreamer-video-1.0 gstreamer-gl-1.0 gstreamer-app-1.0 gstreamer-pbutils-1.0)

if (ENABLE_GLYCIN)
  pkg_check_modules(GLYCIN REQUIRED IMPORTED_TARGET glycin-2)
  set(HAVE_GLYCIN 1)
else()
  set(HAVE_GLYCIN 0)
endif()

if (ENABLE_GDK_PIXBUF)
  pkg_check_modules(GDK_PIXBUF REQUIRED IMPORTED_TARGET gdk-pixbuf-2.0)
  set(HAVE_GDK_PIXBUF 1)
else()
  set(HAVE_GDK_PIXBUF 0)
endif()

# JPEG, ZLIB and Threads
find_package(JPEG REQUIRED)
find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)

# Boost (using header-only components/base)
find_package(Boost REQUIRED)

include(GNUInstallDirs)

# Configuration header
# Porting AC_CHECK_HEADERS/AC_DEFINE from configure.ac
set(HAVE_EXT 1)
set(HAVE_CXX0X 1)
set(GETTEXT_PACKAGE "quiver")
set(PACKAGE_LOCALE_DIR "${CMAKE_INSTALL_FULL_LOCALEDIR}")
set(PACKAGE_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(PACKAGE_DATA_DIR "${CMAKE_INSTALL_FULL_DATADIR}")

# Data directory
if (NOT QUIVER_DATADIR)
  set(QUIVER_DATADIR "${CMAKE_INSTALL_FULL_DATADIR}/quiver")
endif()

# Create config.h
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/config.h.in
  ${CMAKE_CURRENT_BINARY_DIR}/config.h
)

include_directories(
  ${CMAKE_CURRENT_BINARY_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}
)

# glib-genmarshal support
find_program(GLIB_GENMARSHAL NAMES glib-genmarshal REQUIRED)

# Testing (BUILD_TESTING + ctest)
include(CTest)

add_subdirectory(libquiver)
add_subdirectory(src)
if (BUILD_TESTING)
  add_subdirectory(tests)
endif()

# ==============================================================================
# Installation Rules
# ==============================================================================

# 1. UI and asset data files
install(FILES
    data/quiver.ui
    data/quiver-menus.ui
    data/quiver-toolbar.ui
    data/play.png
    data/pause.png
    data/filmstrip.png
    data/filmstrip-big.png
    DESTINATION ${CMAKE_INSTALL_DATADIR}/quiver
)

# 2. Icons directory for application icon lookup (QUIVER_DATADIR/icons)
install(DIRECTORY data/icons/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/quiver/icons
)

# 3. System hicolor icons for desktop environment integration
set(QUIVER_ICON_SIZES 22x22 24x24 26x26 32x32 48x48 64x64 128x128)
foreach(sz ${QUIVER_ICON_SIZES})
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/data/icons/${sz}/quiver-icon-app.png")
        install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/data/icons/${sz}/quiver-icon-app.png"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/${sz}/apps"
            RENAME "quiver-icon-app.png"
        )
        install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/data/icons/${sz}/quiver-icon-app.png"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/${sz}/apps"
            RENAME "quiver.png"
        )
    endif()
endforeach()

# 4. Desktop entry
install(FILES data/quiver.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)

# 5. AppStream metadata
install(FILES data/com.github.bikemike.quiver.metainfo.xml
    DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo
)

# 6. DBus service
install(FILES data/com.github.bikemike.quiver.service
    DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/services
)

# ==============================================================================
# CPack Packaging Configuration
# ==============================================================================
set(CPACK_PACKAGE_NAME "quiver")
set(CPACK_PACKAGE_VENDOR "Mike Morrison")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Fast, feature-rich image viewer and photo manager for GTK4")
set(CPACK_PACKAGE_DESCRIPTION "Quiver is a fast, responsive image and video viewer and organizer built with GTK4. It features high-speed thumbnail caching, lossless JPEG operations, full-screen playback, bookmarks, EXIF metadata editing, and flexible file management.")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/bikemike/quiver")
set(CPACK_PACKAGE_CONTACT "Mike Morrison <bikemike@gmail.com>")
set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")

# Resource files
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/COPYING")
  set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/COPYING")
endif()
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
  set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
endif()

# Default generators
set(CPACK_GENERATOR "DEB;TGZ")
set(CPACK_SOURCE_GENERATOR "TGZ;TXZ")

# Debian package specifics (.deb)
set(CPACK_DEBIAN_PACKAGE_NAME "quiver")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${CPACK_PACKAGE_CONTACT}")
set(CPACK_DEBIAN_PACKAGE_SECTION "graphics")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "${CPACK_PACKAGE_HOMEPAGE_URL}")
find_program(FILE_EXECUTABLE file)
if (FILE_EXECUTABLE)
  set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
else()
  set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
  set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6 (>= 2.38), libgtk-4-1 (>= 4.12.0), libgstreamer1.0-0 (>= 1.2.0), libgstreamer-plugins-base1.0-0, libexiv2-28 | libexiv2-27, libsqlite3-0, libjpeg8, zlib1g")
endif()
set(CPACK_DEBIAN_PACKAGE_GENERATE_SHLIBS ON)
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "gstreamer1.0-plugins-good, gstreamer1.0-plugins-bad, gstreamer1.0-libav")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)

# Source package settings
set(CPACK_SOURCE_IGNORE_FILES
    "/.git/"
    "/build.*/"
    "\\\\.o$"
    "\\\\.a$"
    "\\\\.so$"
    "\\\\.swp$"
    "~$"
)

include(CPack)

