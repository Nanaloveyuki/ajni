#include <stdint.h>

#if defined(AJNI_STANDALONE_JNI)
#define MOONBIT_FFI_EXPORT
typedef uint8_t *moonbit_bytes_t;
#else
#include <moonbit.h>
#endif

#if defined(__ANDROID__)

#include <jni.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "../ajni_android_internal.h"

#if defined(AJNI_USE_MOONBIT_EXPORTS)
extern int32_t ajni_dispatch_webview_event(int32_t kind, int64_t handle,
                                           moonbit_bytes_t payload,
                                           moonbit_bytes_t detail);
#endif

static moonbit_bytes_t ajni_bytes_from_string(JNIEnv *env, jstring text) {
  size_t length = 0;
  char *utf8 = text == NULL ? NULL : ajni_utf8_from_string(env, text, &length);
  if (text != NULL && utf8 == NULL) return NULL;
  if (length > INT32_MAX) {
    free(utf8);
    return NULL;
  }
  moonbit_bytes_t bytes = moonbit_make_bytes((int32_t)length, 0);
  if (bytes != NULL && length > 0) memcpy(bytes, utf8, length);
  free(utf8);
  return bytes;
}

static void ajni_emit_webview(JNIEnv *env, jint kind, jlong handle, jstring payload,
                              jstring detail) {
#if defined(AJNI_USE_MOONBIT_EXPORTS)
  moonbit_bytes_t payload_bytes = ajni_bytes_from_string(env, payload);
  moonbit_bytes_t detail_bytes = ajni_bytes_from_string(env, detail);
  if (payload_bytes == NULL || detail_bytes == NULL) {
    if (payload_bytes != NULL) moonbit_decref(payload_bytes);
    if (detail_bytes != NULL) moonbit_decref(detail_bytes);
    return;
  }
  ajni_dispatch_webview_event(kind, handle, payload_bytes, detail_bytes);
  moonbit_decref(payload_bytes);
  moonbit_decref(detail_bytes);
#else
  (void)env;
  (void)kind;
  (void)handle;
  (void)payload;
  (void)detail;
#endif
}

static void ajni_native_webview_event(JNIEnv *env, jclass unused, jint kind, jlong handle,
                                      jstring payload, jstring detail) {
  (void)unused;
  ajni_emit_webview(env, kind, handle, payload, detail);
}

bool ajni_register_webview_natives(JNIEnv *env, jclass bridge_class) {
  const JNINativeMethod methods[] = {
      {"nativeWebViewEvent", "(IJLjava/lang/String;Ljava/lang/String;)V", (void *)ajni_native_webview_event},
  };
  return (*env)->RegisterNatives(env, bridge_class, methods, sizeof(methods) / sizeof(methods[0])) == JNI_OK &&
         ajni_check_exception(env, "RegisterNatives(WebView)");
}

MOONBIT_FFI_EXPORT int32_t ajni_webview_command(int32_t command, int64_t handle,
                                                 moonbit_bytes_t payload,
                                                 moonbit_bytes_t request_id) {
  if (payload == NULL || request_id == NULL) return 0;
  AjniBridgeLease lease;
  if (!ajni_bridge_lease_acquire(&lease)) return 0;
  JNIEnv *env = lease.env;
  int32_t length = Moonbit_array_length(payload);
  jstring text = ajni_string_from_utf8(env, (const char *)payload, (size_t)length);
  int32_t request_length = Moonbit_array_length(request_id);
  jstring request = ajni_string_from_utf8(env, (const char *)request_id, (size_t)request_length);
  if (text == NULL || request == NULL || !ajni_check_exception(env, "NewString(WebView command)")) {
    if (text != NULL) (*env)->DeleteLocalRef(env, text);
    if (request != NULL) (*env)->DeleteLocalRef(env, request);
    ajni_bridge_lease_release(&lease);
    return 0;
  }
  jmethodID method = (*env)->GetStaticMethodID(env, lease.bridge_class, "webViewCommand",
                                                "(IJLjava/lang/String;Ljava/lang/String;)Z");
  jboolean queued = JNI_FALSE;
  if (method != NULL) {
    queued = (*env)->CallStaticBooleanMethod(env, lease.bridge_class, method, command, (jlong)handle,
                                              text, request);
  }
  int ok = method != NULL && queued == JNI_TRUE && ajni_check_exception(env, "NativeBridge.webViewCommand");
  (*env)->DeleteLocalRef(env, text);
  (*env)->DeleteLocalRef(env, request);
  ajni_bridge_lease_release(&lease);
  return ok ? 1 : 0;
}

MOONBIT_FFI_EXPORT int32_t ajni_webview_set_bounds(int64_t handle, int32_t x, int32_t y,
                                                    int32_t width, int32_t height) {
  AjniBridgeLease lease;
  if (!ajni_bridge_lease_acquire(&lease)) return 0;
  JNIEnv *env = lease.env;
  jmethodID method = (*env)->GetStaticMethodID(env, lease.bridge_class, "webViewSetBounds", "(JIIII)Z");
  jboolean queued = JNI_FALSE;
  if (method != NULL) {
    queued = (*env)->CallStaticBooleanMethod(env, lease.bridge_class, method, (jlong)handle, x, y, width, height);
  }
  int ok = method != NULL && queued == JNI_TRUE && ajni_check_exception(env, "NativeBridge.webViewSetBounds");
  ajni_bridge_lease_release(&lease);
  return ok ? 1 : 0;
}

#else

MOONBIT_FFI_EXPORT int32_t ajni_webview_command(int32_t command, int64_t handle,
                                                 moonbit_bytes_t payload,
                                                 moonbit_bytes_t request_id) {
  (void)command;
  (void)handle;
  (void)payload;
  (void)request_id;
  return 1;
}

MOONBIT_FFI_EXPORT int32_t ajni_webview_set_bounds(int64_t handle, int32_t x, int32_t y,
                                                    int32_t width, int32_t height) {
  (void)handle;
  (void)x;
  (void)y;
  (void)width;
  (void)height;
  return 1;
}

#endif
