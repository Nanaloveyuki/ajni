#include "../runtime/ajni_runtime.h"

#if defined(__has_include)
#if __has_include(<jni.h>)
#define AJNI_UNSAFE_HAS_JNI 1
#endif
#endif
#ifndef AJNI_UNSAFE_HAS_JNI
#define AJNI_UNSAFE_HAS_JNI 0
#endif

MOONBIT_FFI_EXPORT int ajni_unsafe_supported(void) {
  return AJNI_UNSAFE_HAS_JNI;
}

MOONBIT_FFI_EXPORT int ajni_unsafe_host_open(uint64_t env, uint64_t loader) {
  return ajni_rt_host_open((void *)(uintptr_t)env, (void *)(uintptr_t)loader);
}

MOONBIT_FFI_EXPORT int ajni_unsafe_host_enter(uint64_t env) {
  return ajni_rt_host_enter((void *)(uintptr_t)env);
}

MOONBIT_FFI_EXPORT void ajni_unsafe_host_leave(void) {
  ajni_rt_host_leave();
}

MOONBIT_FFI_EXPORT int ajni_unsafe_host_close(uint64_t env) {
  return ajni_rt_host_close((void *)(uintptr_t)env);
}

MOONBIT_FFI_EXPORT void ajni_unsafe_host_throw(uint64_t env) {
  ajni_rt_host_throw((void *)(uintptr_t)env);
}

MOONBIT_FFI_EXPORT int ajni_unsafe_register_companion(uint64_t env) {
  return ajni_rt_register_companion((void *)(uintptr_t)env);
}
