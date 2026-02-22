cmake_minimum_required(VERSION 3.10)
project(clive-cpp)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

find_package(PkgConfig REQUIRED)

pkg_check_modules(GST REQUIRED gstreamer-1.0 gstreamer-webrtc-1.0 gstreamer-sdp-1.0)
pkg_check_modules(JSON REQUIRED json-glib-1.0)
pkg_check_modules(SOUP REQUIRED libsoup-3.0)
pkg_check_modules(GLIB REQUIRED glib-2.0 gobject-2.0)

include_directories(
    ${GST_INCLUDE_DIRS}
    ${JSON_INCLUDE_DIRS}
    ${SOUP_INCLUDE_DIRS}
    ${GLIB_INCLUDE_DIRS}
)

link_directories(
    ${GST_LIBRARY_DIRS}
    ${JSON_LIBRARY_DIRS}
    ${SOUP_LIBRARY_DIRS}
    ${GLIB_LIBRARY_DIRS}
)

add_executable(clive-cpp src/main.cpp src/signaling_client.cpp)

target_compile_definitions(clive-cpp PRIVATE GST_USE_UNSTABLE_API)

target_link_libraries(clive-cpp
    ${GST_LIBRARIES}
    ${JSON_LIBRARIES}
    ${SOUP_LIBRARIES}
    ${GLIB_LIBRARIES}
)
