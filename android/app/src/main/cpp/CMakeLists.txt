cmake_minimum_required(VERSION 3.22.1)
project(ajni C)

set(AJNI_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../../")
get_filename_component(AJNI_ROOT "${AJNI_ROOT}" ABSOLUTE)
if(DEFINED ENV{MOON_HOME})
  set(MOON_HOME "$ENV{MOON_HOME}")
elseif(DEFINED ENV{USERPROFILE})
  set(MOON_HOME "$ENV{USERPROFILE}/.moon")
elseif(DEFINED ENV{HOME})
  set(MOON_HOME "$ENV{HOME}/.moon")
else()
  message(FATAL_ERROR "MoonBit home was not found. Set MOON_HOME before building.")
endif()

find_program(MOON_EXECUTABLE moon REQUIRED)
set(MOONBIT_HOST_SOURCE "${AJNI_ROOT}/_build/native/release/build/android_runtime/android_runtime.c")
set(MOONBIT_RUNTIME_SOURCE "${MOON_HOME}/lib/runtime.c")
set(AJNI_BRIDGE_SOURCE "${AJNI_ROOT}/src/ajni_bridge.c")

if(NOT EXISTS "${MOONBIT_RUNTIME_SOURCE}")
  message(FATAL_ERROR "MoonBit runtime was not found under ${MOON_HOME}. Set MOON_HOME before building.")
endif()

add_custom_command(
  OUTPUT "${MOONBIT_HOST_SOURCE}"
  COMMAND "${MOON_EXECUTABLE}" build --target native --release src/android_runtime
  WORKING_DIRECTORY "${AJNI_ROOT}"
  DEPENDS
    "${AJNI_BRIDGE_SOURCE}"
    "${AJNI_ROOT}/src/android_runtime/main.mbt"
    "${AJNI_ROOT}/src/android_runtime/moon.pkg"
    "${AJNI_ROOT}/src/runtime.mbt"
    "${AJNI_ROOT}/src/types.mbt"
    "${AJNI_ROOT}/src/ffi.mbt"
    "${AJNI_ROOT}/src/android_compat.h"
  COMMENT "Generating MoonBit Android runtime source"
  VERBATIM
)
add_custom_target(generate_moonbit_android_runtime DEPENDS "${MOONBIT_HOST_SOURCE}")

set_source_files_properties("${MOONBIT_HOST_SOURCE}" PROPERTIES GENERATED TRUE)
add_library(ajni SHARED
  "${AJNI_BRIDGE_SOURCE}"
  "${MOONBIT_HOST_SOURCE}"
  "${MOONBIT_RUNTIME_SOURCE}"
)
add_dependencies(ajni generate_moonbit_android_runtime)
target_compile_definitions(ajni PRIVATE AJNI_USE_MOONBIT_EXPORTS=1)
target_include_directories(ajni PRIVATE "${MOON_HOME}/include")
set_source_files_properties("${AJNI_BRIDGE_SOURCE}" PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror")
set_source_files_properties("${MOONBIT_RUNTIME_SOURCE}" PROPERTIES COMPILE_OPTIONS "-include;${AJNI_ROOT}/src/android_compat.h")
target_link_libraries(ajni android log)
