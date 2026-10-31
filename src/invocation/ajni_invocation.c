#include "../runtime/ajni_runtime.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#if defined(__has_include)
#if __has_include(<jni.h>)
#include <jni.h>
#define AJNI_INVOCATION_HAS_JNI 1
#endif
#endif
#ifndef AJNI_INVOCATION_HAS_JNI
#define AJNI_INVOCATION_HAS_JNI 0
#endif

/* Admission is native, not a managed flag that can race across host entry. */
static atomic_int creation_attempted = 0;

typedef struct {
  char **values;
  int count;
  int capacity;
  int allocation_failed;
  int attempted;
  int jni_status;
  int runtime_status;
} AjniInvocationOptions;

static void options_release(void *payload) {
  AjniInvocationOptions *options = payload;
  if (options->values != NULL) {
    for (int i = 0; i < options->count; ++i) free(options->values[i]);
    free(options->values);
    options->values = NULL;
  }
  options->count = 0;
}

MOONBIT_FFI_EXPORT int ajni_invocation_supported(void) {
  return AJNI_INVOCATION_HAS_JNI;
}

MOONBIT_FFI_EXPORT int ajni_invocation_has_attempted(void) {
  return atomic_load_explicit(&creation_attempted, memory_order_acquire);
}

MOONBIT_FFI_EXPORT AjniInvocationOptions *ajni_invocation_options_new(int count) {
  AjniInvocationOptions *options = moonbit_make_external_object(
      options_release, sizeof(AjniInvocationOptions));
  memset(options, 0, sizeof(*options));
  options->capacity = count;
  if (count < 0 || (size_t)count > SIZE_MAX / sizeof(char *)) {
    options->allocation_failed = 1;
  } else if (count != 0) {
    options->values = calloc((size_t)count, sizeof(char *));
    options->allocation_failed = options->values == NULL;
  }
  return options;
}

MOONBIT_FFI_EXPORT int ajni_invocation_options_push(
    AjniInvocationOptions *options, moonbit_bytes_t value) {
  if (options->allocation_failed) return 2;
  if (options->count >= options->capacity || value == NULL) return 3;
  int length = Moonbit_array_length(value);
  if (length < 0 || memchr(value, 0, (size_t)length) != NULL) return 3;
  char *copy = malloc((size_t)length + 1);
  if (copy == NULL) return 2;
  memcpy(copy, value, (size_t)length);
  copy[length] = '\0';
  options->values[options->count++] = copy;
  return 0;
}

MOONBIT_FFI_EXPORT int ajni_invocation_create(
    AjniInvocationOptions *options, uint64_t address) {
#if AJNI_INVOCATION_HAS_JNI
  if (options->allocation_failed) return 2;
  if (options->count != options->capacity || address == 0) return 3;
  JavaVMOption *jvm_options = NULL;
  if (options->count != 0) {
    if ((size_t)options->count > SIZE_MAX / sizeof(JavaVMOption)) return 2;
    jvm_options = calloc((size_t)options->count, sizeof(JavaVMOption));
    if (jvm_options == NULL) return 2;
    for (int i = 0; i < options->count; ++i) {
      jvm_options[i].optionString = options->values[i];
    }
  }
  int expected = 0;
  if (!atomic_compare_exchange_strong_explicit(
          &creation_attempted, &expected, 1,
          memory_order_acq_rel, memory_order_acquire)) {
    free(jvm_options);
    return 1;
  }
  JavaVMInitArgs args;
  memset(&args, 0, sizeof(args));
  args.version = JNI_VERSION_1_8;
  args.nOptions = options->count;
  args.options = jvm_options;
  args.ignoreUnrecognized = JNI_FALSE;
  JavaVM *vm = NULL;
  JNIEnv *env = NULL;
  typedef jint (JNICALL *CreateVm)(JavaVM **, void **, void *);
  CreateVm create = (CreateVm)(uintptr_t)address;
  options->attempted = 1;
  options->jni_status = create(&vm, (void **)&env, &args);
  free(jvm_options);
  options_release(options);
  /* Never DestroyJavaVM or unload after entering the VM's creation routine. */
  if (options->jni_status != JNI_OK) return 4;
  if (vm == NULL || env == NULL) {
    options->jni_status = JNI_ERR;
    return 4;
  }
  options->runtime_status = ajni_rt_host_open(env, NULL);
  if (options->runtime_status == AJNI_RT_OK) {
    options->runtime_status = ajni_rt_register_companion(env);
  }
  return options->runtime_status == AJNI_RT_OK ? 0 : 5;
#else
  (void)options;
  (void)address;
  return 3;
#endif
}

MOONBIT_FFI_EXPORT int ajni_invocation_attempted(AjniInvocationOptions *options) {
  return options->attempted;
}

MOONBIT_FFI_EXPORT int ajni_invocation_jni_status(AjniInvocationOptions *options) {
  return options->jni_status;
}

MOONBIT_FFI_EXPORT int ajni_invocation_runtime_status(AjniInvocationOptions *options) {
  return options->runtime_status;
}
