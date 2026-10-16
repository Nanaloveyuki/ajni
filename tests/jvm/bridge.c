#include <jni.h>
#include <moonbit.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/runtime/ajni_runtime.h"

extern void moonbit_runtime_init(int argc, char **argv);
extern void moonbit_init(void);
extern int32_t ajni_smoke_run(int32_t stage);

static JavaVM *fixture_vm;
static pthread_mutex_t fixture_lock = PTHREAD_MUTEX_INITIALIZER;
static int fixture_initialized;
static _Atomic int32_t managed_entries;
static char *fixture_failure;

MOONBIT_FFI_EXPORT void ajni_fixture_entry(void) {
  atomic_fetch_add(&managed_entries, 1);
}

MOONBIT_FFI_EXPORT void ajni_fixture_failure(moonbit_string_t message) {
  free(fixture_failure);
  int32_t length = Moonbit_array_length(message);
  fixture_failure = malloc((size_t)length + 1);
  if (fixture_failure == NULL) return;
  for (int32_t i = 0; i < length; ++i)
    fixture_failure[i] = message[i] <= 127 ? (char)message[i] : '?';
  fixture_failure[length] = 0;
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
  (void)reserved;
  JNIEnv *env = NULL;
  fixture_vm = vm;
  if ((*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) != JNI_OK)
    return JNI_ERR;
  if (ajni_rt_register_companion(env) != 0) return JNI_ERR;
  return JNI_VERSION_1_6;
}

JNIEXPORT void JNICALL
Java_dev_nanaloveyuki_ajni_fixture_Smoke_initialize(JNIEnv *env, jclass cls,
                                                 jobject loader) {
  (void)cls;
  pthread_mutex_lock(&fixture_lock);
  if (fixture_initialized) {
    pthread_mutex_unlock(&fixture_lock);
    jclass error = (*env)->FindClass(env, "java/lang/IllegalStateException");
    if (error != NULL) (*env)->ThrowNew(env, error, "Already initialized");
    return;
  }
  JavaVM *vm = NULL;
  if ((*env)->GetJavaVM(env, &vm) != JNI_OK || vm != fixture_vm) {
    pthread_mutex_unlock(&fixture_lock);
    jclass error = (*env)->FindClass(env, "java/lang/IllegalStateException");
    if (error != NULL) (*env)->ThrowNew(env, error, "Unexpected Java VM");
    return;
  }
  moonbit_runtime_init(0, NULL);
  moonbit_init();
  int status = ajni_rt_host_open(env, loader);
  if (status == 0) fixture_initialized = 1;
  pthread_mutex_unlock(&fixture_lock);
  if (status != 0) ajni_rt_host_throw(env);
}

JNIEXPORT jint JNICALL
Java_dev_nanaloveyuki_ajni_fixture_Smoke_run(JNIEnv *env, jclass cls, jint stage) {
  (void)cls;
  if (ajni_rt_host_enter(env) != 0) {
    ajni_rt_host_throw(env);
    return -1;
  }
  atomic_fetch_add(&managed_entries, 1);
  int32_t result = ajni_smoke_run(stage);
  ajni_rt_host_leave();
  if (result != 0) {
    jclass error = (*env)->FindClass(env, "java/lang/AssertionError");
    if (error != NULL)
      (*env)->ThrowNew(env, error, fixture_failure == NULL ? "MoonBit fixture failed" : fixture_failure);
  }
  return result;
}

JNIEXPORT void JNICALL
Java_dev_nanaloveyuki_ajni_fixture_Smoke_close(JNIEnv *env, jclass cls) {
  (void)cls;
  if (ajni_rt_host_close(env) != 0) ajni_rt_host_throw(env);
}

JNIEXPORT void JNICALL
Java_dev_nanaloveyuki_ajni_fixture_Smoke_reopen(JNIEnv *env, jclass cls,
                                             jobject loader) {
  (void)cls;
  if (ajni_rt_host_open(env, loader) != 0) ajni_rt_host_throw(env);
}

JNIEXPORT jint JNICALL
Java_dev_nanaloveyuki_ajni_fixture_Smoke_entries(JNIEnv *env, jclass cls) {
  (void)env;
  (void)cls;
  return atomic_load(&managed_entries);
}
