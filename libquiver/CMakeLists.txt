# Generate marshallers
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/quiver-marshallers.h
    COMMAND ${GLIB_GENMARSHAL} --header --prefix=_quiver_marshal ${CMAKE_CURRENT_SOURCE_DIR}/quiver-marshallers.list > ${CMAKE_CURRENT_BINARY_DIR}/quiver-marshallers.h
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/quiver-marshallers.list
)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/quiver-marshallers.c
    COMMAND echo "\#include \"quiver-marshallers.h\"" > ${CMAKE_CURRENT_BINARY_DIR}/quiver-marshallers.c
    COMMAND ${GLIB_GENMARSHAL} --body --prefix=_quiver_marshal ${CMAKE_CURRENT_SOURCE_DIR}/quiver-marshallers.list >> ${CMAKE_CURRENT_BINARY_DIR}/quiver-marshallers.c
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/quiver-marshallers.list ${CMAKE_CURRENT_BINARY_DIR}/quiver-marshallers.h
)

add_library(quiver_lib STATIC
    ${CMAKE_CURRENT_BINARY_DIR}/quiver-marshallers.c
    ${CMAKE_CURRENT_BINARY_DIR}/quiver-marshallers.h
    quiver-pixbuf-utils.c
    quiver-pixbuf-utils.h
    quiver-image-view.c
    quiver-image-view.h
    quiver-icon-view.c
    quiver-icon-view.h
    quiver-navigation-control.c
    quiver-navigation-control.h
)

target_include_directories(quiver_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(quiver_lib PUBLIC
    PkgConfig::GTK4
    PkgConfig::GIO
)
