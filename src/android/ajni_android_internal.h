#ifndef AJNI_ANDROID_INTERNAL_H
#define AJNI_ANDROID_INTERNAL_H

#include <stdint.h>

enum {
  AJNI_OK = 1,
  AJNI_NOT_INITIALIZED = 0,
  AJNI_RUNTIME_DESTROYED = -1,
  AJNI_NATIVE_FAILURE = -2,
  AJNI_JAVA_EXCEPTION = -3,
  AJNI_UNSUPPORTED = -4,
};

void ajni_reset_error(void);
int32_t ajni_set_error(int32_t status, const char *message);
int32_t ajni_error_status(void);

#if defined(__ANDROID__)

#include <stddef.h>
#include <stdbool.h>
#include <jni.h>

extern JavaVM *g_vm;
extern jclass g_bridge_class;

typedef struct {
  JavaVM *vm;
  JNIEnv *env;
  jclass bridge_class;
  bool attached;
  int64_t runtime_generation;
} AjniBridgeLease;

bool ajni_check_exception(JNIEnv *env, const char *operation);
void ajni_throw_new(JNIEnv *env, const char *class_name, const char *message);
jstring ajni_string_from_utf8(JNIEnv *env, const char *text, size_t length);
char *ajni_utf8_from_string(JNIEnv *env, jstring text, size_t *out_length);
int32_t ajni_bridge_lease_acquire(AjniBridgeLease *lease);
void ajni_bridge_lease_release(AjniBridgeLease *lease);
bool ajni_register_webview_natives(JNIEnv *env, jclass bridge_class);

#endif

#endif
