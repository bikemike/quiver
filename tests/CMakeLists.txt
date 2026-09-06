cmake_minimum_required(VERSION 3.10)

find_package(Catch2 3 REQUIRED)
include(Catch)

set(UNIT_TEST_SOURCES
    test_main.cpp
    unit/test_md5.cpp
    unit/test_strnatcmp.cpp
    unit/test_timer.cpp
    unit/test_image_cache.cpp
    unit/test_browser_history.cpp
    unit/test_file_conflict_check.cpp
    unit/test_preferences.cpp
    unit/test_bookmarks.cpp
    unit/test_external_tools.cpp
    unit/test_image_list_filter.cpp
    unit/test_quiver_file.cpp
    unit/test_pixbuf_utils.cpp
    unit/test_quiver_utils.cpp
    unit/test_task_manager.cpp
    unit/test_icon_view.cpp
    unit/test_library_exiv2.cpp
    unit/test_library_libjpeg.cpp
    unit/test_library_ffmpeg.cpp
    unit/test_library_gstreamer.cpp
    unit/test_library_glib_gio.cpp
    unit/test_image_decoder.cpp
    unit/test_rename_task.cpp
    unit/test_organize_task.cpp
    unit/test_adjust_date_task.cpp
    unit/test_headerbar_toolbar.cpp
)

add_executable(quiver_unit_tests ${UNIT_TEST_SOURCES})

target_compile_definitions(quiver_unit_tests PRIVATE
    QUIVER_TEST_IMAGES_DIR_DEF="${CMAKE_CURRENT_SOURCE_DIR}/images"
    QUIVER_DATA_DIR_DEF="${CMAKE_SOURCE_DIR}/data"
)

target_include_directories(quiver_unit_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/libquiver
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}
)

target_link_libraries(quiver_unit_tests PRIVATE
    quiver_core
    Catch2::Catch2
)

# Register with CTest
catch_discover_tests(quiver_unit_tests
    PROPERTIES LABELS "unit"
)

# Custom test runners & display targets
add_custom_target(check
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run_tests.sh
    DEPENDS quiver_unit_tests
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running Catch2 unit test suite via run_tests.sh"
)

add_custom_target(check-wayland
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run_tests.sh --wayland
    DEPENDS quiver_unit_tests
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running Catch2 unit tests under Wayland (wlheadless-run + weston)"
)

add_custom_target(check-x11
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run_tests.sh --x11
    DEPENDS quiver_unit_tests
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running Catch2 unit tests under X11 (xvfb-run)"
)

add_custom_target(check-both
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run_tests.sh --both
    DEPENDS quiver_unit_tests
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running Catch2 unit tests under both Wayland and X11"
)

add_custom_target(check-valgrind
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run_tests.sh --valgrind
    DEPENDS quiver_unit_tests
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running Catch2 unit tests under Valgrind Memcheck"
)

# Clang-Tidy target for static analysis
find_program(CLANG_TIDY clang-tidy)
if (CLANG_TIDY)
    add_custom_target(tidy
        COMMAND ${CLANG_TIDY}
            -p ${CMAKE_BINARY_DIR}
            ${CMAKE_SOURCE_DIR}/src/MD5.cpp
            ${CMAKE_SOURCE_DIR}/src/ImageCache.cpp
            ${CMAKE_SOURCE_DIR}/src/Preferences.cpp
            ${CMAKE_SOURCE_DIR}/src/BrowserHistory.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/test_main.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_md5.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_strnatcmp.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_image_cache.cpp
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Running clang-tidy static analysis"
    )
endif()

# Standalone performance and legacy test binaries (now linking quiver_core)
add_executable(test_image_decoder test_image_decoder.cpp)
target_include_directories(test_image_decoder PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}
)
target_link_libraries(test_image_decoder PRIVATE quiver_core)

add_executable(test_video_loader test_video_loader.cpp)
target_include_directories(test_video_loader PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}
)
target_link_libraries(test_video_loader PRIVATE quiver_core)

add_executable(test_icon_view test_icon_view.cpp)
target_include_directories(test_icon_view PRIVATE
    ${CMAKE_SOURCE_DIR}/libquiver
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}
)
target_link_libraries(test_icon_view PRIVATE quiver_core)
