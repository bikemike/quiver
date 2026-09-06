add_executable(test_image_decoder
    test_image_decoder.cpp
    ${CMAKE_SOURCE_DIR}/src/ImageDecoder.cpp
    ${CMAKE_SOURCE_DIR}/src/QuiverVideoOps.cpp
)

target_include_directories(test_image_decoder PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}
)

target_link_libraries(test_image_decoder PRIVATE
    quiver_lib
    PkgConfig::GTK4
    PkgConfig::GIO
    PkgConfig::FFMPEG
)

if (TARGET PkgConfig::GLYCIN)
    target_link_libraries(test_image_decoder PRIVATE PkgConfig::GLYCIN)
endif()

add_test(NAME ImageDecoderTest COMMAND test_image_decoder)

add_executable(test_video_loader
    test_video_loader.cpp
    ${CMAKE_SOURCE_DIR}/src/QuiverVideoOps.cpp
    ${CMAKE_SOURCE_DIR}/src/QuiverFile.cpp
    ${CMAKE_SOURCE_DIR}/src/Timer.cpp
    ${CMAKE_SOURCE_DIR}/src/ImageDecoder.cpp
    ${CMAKE_SOURCE_DIR}/src/ImageCache.cpp
    ${CMAKE_SOURCE_DIR}/src/QuiverUtils.cpp
    ${CMAKE_SOURCE_DIR}/src/MD5.cpp
    ${CMAKE_SOURCE_DIR}/src/ThreadUtil.cpp
)

target_include_directories(test_video_loader PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}
)

target_link_libraries(test_video_loader PRIVATE
    quiver_lib
    PkgConfig::GTK4
    PkgConfig::GIO
    PkgConfig::EXIV2
    PkgConfig::FFMPEG
    PkgConfig::GSTREAMER
    JPEG::JPEG
    Threads::Threads
    Boost::boost
)

if (TARGET PkgConfig::GLYCIN)
    target_link_libraries(test_video_loader PRIVATE PkgConfig::GLYCIN)
endif()

add_test(NAME VideoLoaderTest COMMAND test_video_loader)

add_executable(test_icon_view
    test_icon_view.cpp
    ${CMAKE_SOURCE_DIR}/src/ImageCache.cpp
)

target_include_directories(test_icon_view PRIVATE
    ${CMAKE_SOURCE_DIR}/libquiver
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}
)

target_link_libraries(test_icon_view PRIVATE
    quiver_lib
    PkgConfig::GTK4
    PkgConfig::GIO
    Threads::Threads
)

add_test(NAME IconViewTest COMMAND test_icon_view)
