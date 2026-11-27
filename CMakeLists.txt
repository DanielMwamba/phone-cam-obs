cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" OFF)
option(ENABLE_QT "Use Qt functionality" OFF)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_package(Qt6 COMPONENTS Widgets Core)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
  )
endif()

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/plugin-main.cpp
    src/phonecam-source.cpp
    src/phonecam-source.hpp
    src/packet-queue.cpp
    src/packet-queue.hpp
    src/adb-forward.cpp
    src/adb-forward.hpp
    src/pcam-auth.cpp
    src/pcam-auth.hpp
    src/pcam-reader.cpp
    src/pcam-reader.hpp
    src/tcp-socket.cpp
    src/tcp-socket.hpp
    src/h264-decoder.cpp
    src/h264-decoder.hpp
)

# FFmpeg from the prebuilt obs-deps: same major versions as the DLLs OBS ships (avcodec-62,
# avutil-60), so the plugin binds to OBS's own copies at runtime.
find_path(FFMPEG_INCLUDE_DIR libavcodec/avcodec.h REQUIRED)
find_library(AVCODEC_LIBRARY avcodec REQUIRED)
find_library(AVUTIL_LIBRARY avutil REQUIRED)
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE "${FFMPEG_INCLUDE_DIR}")
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE "${AVCODEC_LIBRARY}" "${AVUTIL_LIBRARY}")

if(OS_WINDOWS)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ws2_32)
  # winsock2.h must win over the old winsock.h that windows.h would pull in
  target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
