#ifndef AJNI_ANDROID_INTERNAL_H
#define AJNI_ANDROID_INTERNAL_H

#if defined(__ANDROID__)

#include <stddef.h>
#include <stdbool.h>
#include <jni.h>

extern JavaVM *g_vm;
extern jclass g_bridge_class;
extern bool g_initialized;

bool ajni_check_exception(JNIEnv *env, const char *operation);
jstring ajni_string_from_utf8(JNIEnv *env, const char *text, size_t length);
char *ajni_utf8_from_string(JNIEnv *env, jstring text, size_t *out_length);
bool ajni_register_webview_natives(JNIEnv *env, jclass bridge_class);

#endif

#endif
