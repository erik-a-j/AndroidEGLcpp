cmake_minimum_required(VERSION 3.22.1)
project(egl C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(DEPS_DIR "$ENV{HOME}/src/third_party")
set(FREETYPE_SRC "${DEPS_DIR}/freetype")
set(HARFBUZZ_SRC "${DEPS_DIR}/harfbuzz")

if (NOT EXISTS "${FREETYPE_SRC}/CMakeLists.txt" OR NOT EXISTS "${HARFBUZZ_SRC}/CMakeLists.txt")
  execute_process(
    COMMAND bash "${CMAKE_SOURCE_DIR}/../tools/getdeps.sh" "${DEPS_DIR}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    RESULT_VARIABLE rc
  )
  if (NOT rc EQUAL 0)
    message(FATAL_ERROR "getdeps.sh failed with exit code ${rc}")
  endif()
endif()

set(FT_DISABLE_ZLIB TRUE CACHE BOOL "")
set(FT_DISABLE_BZIP2 TRUE CACHE BOOL "")
set(FT_DISABLE_PNG TRUE CACHE BOOL "")
set(FT_DISABLE_HARFBUZZ TRUE CACHE BOOL "")
set(HB_HAVE_FREETYPE TRUE CACHE BOOL "")
add_subdirectory("${FREETYPE_SRC}" "${CMAKE_BINARY_DIR}/_deps/freetype-build")
add_subdirectory("${HARFBUZZ_SRC}" "${CMAKE_BINARY_DIR}/_deps/harfbuzz-build")


# Build glue directly into the shared library so ANativeActivity_onCreate is present.
add_library(native-lib SHARED
    main.cpp
    assets.cpp
    javahack.cpp
    ui_renderer.cpp
    text_renderer.cpp
    ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c
)

target_include_directories(native-lib PRIVATE
    ${ANDROID_NDK}/sources/android/native_app_glue
)

target_link_libraries(native-lib
    freetype
    harfbuzz
    android log EGL GLESv3 m
)
