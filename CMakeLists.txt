cmake_minimum_required(VERSION 3.10)
if (POLICY CMP0167)
  cmake_policy(SET CMP0167 OLD)
endif()
project(quiver VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra)

# Find pkg-config
find_package(PkgConfig REQUIRED)

# Find Packages
pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4>=4.12)
pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0)
pkg_check_modules(EXIV2 REQUIRED IMPORTED_TARGET exiv2)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil libswscale)
pkg_check_modules(LIBSQLITE REQUIRED IMPORTED_TARGET sqlite3)
pkg_check_modules(GSTREAMER REQUIRED IMPORTED_TARGET gstreamer-1.0 gstreamer-plugins-base-1.0 gstreamer-video-1.0 gstreamer-gl-1.0 gstreamer-app-1.0 gstreamer-pbutils-1.0)

# JPEG and Threads
find_package(JPEG REQUIRED)
find_package(Threads REQUIRED)

# Boost (using header-only components/base)
find_package(Boost REQUIRED)

# Configuration header
# Porting AC_CHECK_HEADERS/AC_DEFINE from configure.ac
set(HAVE_EXT 1)
set(HAVE_CXX0X 1)
set(GETTEXT_PACKAGE "quiver")
set(PACKAGE_LOCALE_DIR "${CMAKE_INSTALL_PREFIX}/share/locale")
set(PACKAGE_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(PACKAGE_DATA_DIR "${CMAKE_INSTALL_PREFIX}/share")

# Use absolute data directory for now or default
if (NOT QUIVER_DATADIR)
  set(QUIVER_DATADIR "${CMAKE_CURRENT_SOURCE_DIR}/data")
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
# TODO: tests directory is not present; re-enable when it exists
#add_subdirectory(tests)
