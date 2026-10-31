cmake_minimum_required(VERSION 3.22)
project(ajni_ohos_mvp C CXX)

# The SDK is self-contained LLVM; its GCC-toolchain hint is rejected by x86_64 Clang.
unset(CMAKE_C_COMPILER_EXTERNAL_TOOLCHAIN)
unset(CMAKE_CXX_COMPILER_EXTERNAL_TOOLCHAIN)
unset(CMAKE_ASM_COMPILER_EXTERNAL_TOOLCHAIN)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PROBE_GENERATED "${CMAKE_CURRENT_SOURCE_DIR}/generated/probe.c")
set(PROBE_RUNTIME "${CMAKE_CURRENT_SOURCE_DIR}/moonbit/runtime")
if(NOT EXISTS "${PROBE_GENERATED}" OR NOT EXISTS "${PROBE_RUNTIME}/runtime.c")
  message(FATAL_ERROR "Run scripts/build-ohos-mvp.py to stage the pinned MoonBit C inputs first.")
endif()

add_library(ajni_ohos_probe SHARED
  probe.cpp
  "${PROBE_GENERATED}"
  "${PROBE_RUNTIME}/runtime.c"
  "${PROBE_RUNTIME}/utf.c"
  "${PROBE_RUNTIME}/env.c"
  "${PROBE_RUNTIME}/sync_io.c"
  "${PROBE_RUNTIME}/backtrace.c"
)
target_include_directories(ajni_ohos_probe PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/moonbit/include"
)
target_compile_options(ajni_ohos_probe PRIVATE -fwrapv -fno-strict-aliasing)
set_source_files_properties(probe.cpp PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror")
set_source_files_properties("${PROBE_GENERATED}" PROPERTIES
  COMPILE_DEFINITIONS "main=ajni_ohos_unused_main"
)
target_link_options(ajni_ohos_probe PRIVATE -Wl,--no-undefined)
target_link_libraries(ajni_ohos_probe PUBLIC libace_napi.z.so libhilog_ndk.z.so)
