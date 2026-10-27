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
# Each ABI/configuration has its own MoonBit build graph and generated C file.
# Gradle can schedule ABI builds in parallel without sharing MoonBit's lock/output.
set(AJNI_MOONBIT_TARGET_DIR "${CMAKE_CURRENT_BINARY_DIR}/moonbit")
set(MOONBIT_HOST_SOURCE "${AJNI_MOONBIT_TARGET_DIR}/native/release/build/android_runtime/android_runtime.c")
set(MOONBIT_RUNTIME_SOURCES
  "${MOON_HOME}/lib/runtime/runtime.c"
  "${MOON_HOME}/lib/runtime/utf.c"
  "${MOON_HOME}/lib/runtime/env.c"
  "${MOON_HOME}/lib/runtime/sync_io.c"
  "${MOON_HOME}/lib/runtime/backtrace.c"
)
if(NOT EXISTS "${MOON_HOME}/lib/runtime/runtime.c")
  set(MOONBIT_RUNTIME_SOURCES "${MOON_HOME}/lib/runtime.c")
endif()
set(AJNI_BRIDGE_SOURCE "${AJNI_ROOT}/src/android/ajni_bridge.c")
set(AJNI_WEBVIEW_BRIDGE_SOURCE "${AJNI_ROOT}/src/webview/ajni_webview_bridge.c")
set(AJNI_CORE_SOURCE "${AJNI_ROOT}/src/runtime/ajni_runtime.c")

list(GET MOONBIT_RUNTIME_SOURCES 0 MOONBIT_RUNTIME_SOURCE)
if(NOT EXISTS "${MOONBIT_RUNTIME_SOURCE}")
  message(FATAL_ERROR "MoonBit runtime was not found under ${MOON_HOME}. Set MOON_HOME before building.")
endif()

# CONFIGURE_DEPENDS also refreshes the input list when a source/package is added
# or removed. Include all package/module settings, native stubs and headers;
# a hand-maintained list misses imported packages and transitive C includes.
file(GLOB_RECURSE AJNI_SOURCE_INPUTS CONFIGURE_DEPENDS
  "${AJNI_ROOT}/src/*.mbt"
  "${AJNI_ROOT}/src/*.mbt.md"
  "${AJNI_ROOT}/src/*.c"
  "${AJNI_ROOT}/src/*.h"
  "${AJNI_ROOT}/src/moon.pkg"
  "${AJNI_ROOT}/src/moon.pkg.json"
)
file(GLOB AJNI_MODULE_INPUTS CONFIGURE_DEPENDS
  "${AJNI_ROOT}/moon.mod"
  "${AJNI_ROOT}/moon.mod.json"
  "${AJNI_ROOT}/moon.lock"
  "${AJNI_ROOT}/moon.lock.json"
  "${AJNI_ROOT}/.moon-version"
  "${AJNI_ROOT}/.moonbit-toolchain"
)
file(GLOB_RECURSE AJNI_DEPENDENCY_INPUTS CONFIGURE_DEPENDS
  "${AJNI_ROOT}/.mooncakes/*.mbt"
  "${AJNI_ROOT}/.mooncakes/*.mbt.md"
  "${AJNI_ROOT}/.mooncakes/*.c"
  "${AJNI_ROOT}/.mooncakes/*.h"
  "${AJNI_ROOT}/.mooncakes/*.mi"
  "${AJNI_ROOT}/.mooncakes/*.core"
  "${AJNI_ROOT}/.mooncakes/*.o"
  "${AJNI_ROOT}/.mooncakes/*.a"
  "${AJNI_ROOT}/.mooncakes/moon.pkg"
  "${AJNI_ROOT}/.mooncakes/moon.pkg.json"
  "${AJNI_ROOT}/.mooncakes/moon.mod"
  "${AJNI_ROOT}/.mooncakes/moon.mod.json"
)
file(GLOB_RECURSE MOONBIT_TOOLCHAIN_INPUTS CONFIGURE_DEPENDS
  "${MOON_HOME}/include/*.h"
  "${MOON_HOME}/lib/core/*.mbt"
  "${MOON_HOME}/lib/core/_build/native/release/bundle/*.mi"
  "${MOON_HOME}/lib/core/_build/native/release/bundle/*.core"
  "${MOON_HOME}/lib/runtime/*.h"
  "${MOON_HOME}/lib/core/moon.pkg"
  "${MOON_HOME}/lib/core/moon.pkg.json"
  "${MOON_HOME}/lib/core/moon.mod"
  "${MOON_HOME}/lib/core/moon.mod.json"
)
file(GLOB MOONBIT_COMPILER_INPUTS CONFIGURE_DEPENDS
  "${MOON_HOME}/bin/moonc"
  "${MOON_HOME}/bin/moonc.exe"
)
# Changing the dependency set itself (including removals) must invalidate C.
# file(CONFIGURE) updates this manifest only when its content changes.
set(AJNI_MOONBIT_INPUT_MANIFEST "${CMAKE_CURRENT_BINARY_DIR}/moonbit-inputs.txt")
file(CONFIGURE OUTPUT "${AJNI_MOONBIT_INPUT_MANIFEST}"
  CONTENT "${AJNI_SOURCE_INPUTS}\n${AJNI_MODULE_INPUTS}\n${AJNI_DEPENDENCY_INPUTS}\n${MOONBIT_TOOLCHAIN_INPUTS}\n${MOONBIT_COMPILER_INPUTS}\n"
  @ONLY
)

add_custom_command(
  OUTPUT "${MOONBIT_HOST_SOURCE}"
  COMMAND "${MOON_EXECUTABLE}" build --target native --release
    --target-dir "${AJNI_MOONBIT_TARGET_DIR}" src/android_runtime
  WORKING_DIRECTORY "${AJNI_ROOT}"
  DEPENDS
    "${AJNI_MOONBIT_INPUT_MANIFEST}"
    "${CMAKE_CURRENT_LIST_FILE}"
    ${AJNI_SOURCE_INPUTS}
    ${AJNI_MODULE_INPUTS}
    ${AJNI_DEPENDENCY_INPUTS}
    ${MOONBIT_TOOLCHAIN_INPUTS}
    ${MOONBIT_COMPILER_INPUTS}
    "${MOON_EXECUTABLE}"
    ${MOONBIT_RUNTIME_SOURCES}
  COMMENT "Generating MoonBit Android runtime source for ${ANDROID_ABI}"
  VERBATIM
)
add_custom_target(generate_moonbit_android_runtime DEPENDS "${MOONBIT_HOST_SOURCE}")

set_source_files_properties("${MOONBIT_HOST_SOURCE}" PROPERTIES GENERATED TRUE)
add_library(ajni SHARED
  "${AJNI_CORE_SOURCE}"
  "${AJNI_BRIDGE_SOURCE}"
  "${AJNI_WEBVIEW_BRIDGE_SOURCE}"
  "${MOONBIT_HOST_SOURCE}"
  ${MOONBIT_RUNTIME_SOURCES}
)
add_dependencies(ajni generate_moonbit_android_runtime)
target_compile_definitions(ajni PRIVATE AJNI_USE_MOONBIT_EXPORTS=1 AJNI_FEATURE_WEBVIEW=1)
target_include_directories(ajni PRIVATE "${MOON_HOME}/include")
set_source_files_properties("${AJNI_BRIDGE_SOURCE}" PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror")
set_source_files_properties("${AJNI_WEBVIEW_BRIDGE_SOURCE}" PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror")
set_source_files_properties("${AJNI_CORE_SOURCE}" PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror")
set_source_files_properties(${MOONBIT_RUNTIME_SOURCES} PROPERTIES COMPILE_OPTIONS "-include;${AJNI_ROOT}/src/android/android_compat.h")
target_link_libraries(ajni android log)
