#include <jni.h>
#include <moonbit.h>
#include <stdint.h>
#include <stdlib.h>

/* The script adds the ajni runtime source directory to the include search. */
#include "ajni_runtime.h"

static jint JNICALL fixture_native_add(JNIEnv *env, jclass type, jint value) {
  (void)type;
  int status = ajni_rt_host_enter(env);
  if (status != AJNI_RT_OK) {
    ajni_rt_host_throw(env);
    return 0;
  }
  ajni_rt_host_leave();
  return value + 3;
}

MOONBIT_FFI_EXPORT uint64_t ajni_invocation_fixture_native_address(void) {
  return (uint64_t)(uintptr_t)fixture_native_add;
}

MOONBIT_FFI_EXPORT void ajni_invocation_fixture_delete_local(
    uint64_t env_address, uint64_t reference) {
  JNIEnv *env = (JNIEnv *)(uintptr_t)env_address;
  (*env)->DeleteLocalRef(env, (jobject)(uintptr_t)reference);
}

MOONBIT_FFI_EXPORT void ajni_invocation_fixture_failure(void) {
  exit(1);
}
