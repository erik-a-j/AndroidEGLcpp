cmake_minimum_required(VERSION 3.22.1)
project(egl C CXX)

#set(CMAKE_C_STANDARD 11)
#set(CMAKE_C_STANDARD_REQUIRED ON)

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

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_custom_target(copy_compile_commands ALL
    COMMAND ${CMAKE_COMMAND} -E copy
    "${CMAKE_BINARY_DIR}/compile_commands.json"
    "${CMAKE_SOURCE_DIR}/compile_commands.json"
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Copying compile_commands.json to source directory"
)
add_custom_target(copy_font ALL
    COMMAND bash ${CMAKE_SOURCE_DIR}/../tools/copyfont.sh
    "${CMAKE_SOURCE_DIR}/../assets"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Copying font to assets directory"
)

# Build glue directly into the shared library so ANativeActivity_onCreate is present.
add_library(native-lib SHARED
    main.cpp
    javahack.cpp
    ui_renderer.cpp
    text_renderer.cpp
    ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c
)

target_include_directories(native-lib PRIVATE
    ${ANDROID_NDK}/sources/android/native_app_glue
)
add_dependencies(native-lib copy_font copy_compile_commands)

set(FT_DISABLE_ZLIB TRUE CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 TRUE CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG TRUE CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ TRUE CACHE BOOL "" FORCE) # we link hb separately

add_subdirectory("${FREETYPE_SRC}" "${CMAKE_BINARY_DIR}/_deps/freetype-build")

# HarfBuzz (enable freetype)
set(HB_HAVE_FREETYPE TRUE CACHE BOOL "" FORCE)

add_subdirectory("${HARFBUZZ_SRC}" "${CMAKE_BINARY_DIR}/_deps/harfbuzz-build")

target_link_libraries(native-lib
    freetype
    harfbuzz
    android log EGL GLESv3 m
)
