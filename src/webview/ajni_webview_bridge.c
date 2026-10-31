#include <stdint.h>

#if defined(AJNI_STANDALONE_JNI)
#define MOONBIT_FFI_EXPORT
typedef uint8_t *moonbit_bytes_t;
#else
#include <moonbit.h>
#endif

#include "../android/ajni_android_internal.h"

#if defined(__ANDROID__)

#include <jni.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>


#if defined(AJNI_USE_MOONBIT_EXPORTS)
extern void ajni_dispatch_webview_event(int32_t kind, int64_t handle,
                                           moonbit_bytes_t payload,
                                           moonbit_bytes_t detail);
#endif

#if defined(AJNI_USE_MOONBIT_EXPORTS)
static moonbit_bytes_t ajni_bytes_from_string(JNIEnv *env, jstring text) {
  size_t length = 0;
  char *utf8 = text == NULL ? NULL : ajni_utf8_from_string(env, text, &length);
  if (text != NULL && utf8 == NULL) {
    ajni_throw_new(env, "java/lang/OutOfMemoryError", "Could not convert WebView event string");
    return NULL;
  }
  if (length > INT32_MAX) {
    free(utf8);
    ajni_throw_new(env, "java/lang/IllegalArgumentException", "WebView event string is too large");
    return NULL;
  }
  moonbit_bytes_t bytes = moonbit_make_bytes((int32_t)length, 0);
  if (bytes == NULL) ajni_throw_new(env, "java/lang/OutOfMemoryError", "Could not allocate WebView event bytes");
  if (bytes != NULL && length > 0) memcpy(bytes, utf8, length);
  free(utf8);
  return bytes;
}
#endif

static void ajni_emit_webview(JNIEnv *env, jint kind, jlong handle, jstring payload,
                              jstring detail) {
#if defined(AJNI_USE_MOONBIT_EXPORTS)
  moonbit_bytes_t payload_bytes = ajni_bytes_from_string(env, payload);
  if (payload_bytes == NULL) return;
  moonbit_bytes_t detail_bytes = ajni_bytes_from_string(env, detail);
  if (detail_bytes == NULL) {
    moonbit_decref(payload_bytes);
    return;
  }
  ajni_dispatch_webview_event(kind, handle, payload_bytes, detail_bytes);
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
  if (!ajni_android_enter(env)) return;
  ajni_emit_webview(env, kind, handle, payload, detail);
  ajni_android_leave(env);
}

bool ajni_register_webview_natives(JNIEnv *env, jclass bridge_class) {
  const JNINativeMethod methods[] = {
      {"nativeWebViewEvent", "(IJLjava/lang/String;Ljava/lang/String;)V", (void *)ajni_native_webview_event},
  };
  jint result = (*env)->RegisterNatives(env, bridge_class, methods, sizeof(methods) / sizeof(methods[0]));
  bool exception_pending = (*env)->ExceptionCheck(env);
  return !exception_pending && result == JNI_OK;
}

MOONBIT_FFI_EXPORT int32_t ajni_webview_command(int32_t command, int64_t handle,
                                                 moonbit_bytes_t payload,
                                                 moonbit_bytes_t request_id) {
  ajni_reset_error();
  if (payload == NULL || request_id == NULL) return ajni_set_error(AJNI_NATIVE_FAILURE, "Missing WebView command payload");
  AjniBridgeLease lease;
  int32_t status = ajni_bridge_lease_acquire(&lease);
  if (status != AJNI_OK) return status;
  JNIEnv *env = lease.env;
  jstring request = NULL;
  jstring text = ajni_string_from_utf8(env, (const char *)payload, (size_t)Moonbit_array_length(payload));
  bool exception_free = ajni_check_exception(env, "NewString(WebView payload)");
  if (!exception_free || text == NULL) goto failure;
  request = ajni_string_from_utf8(env, (const char *)request_id, (size_t)Moonbit_array_length(request_id));
  exception_free = ajni_check_exception(env, "NewString(WebView request)");
  if (!exception_free || request == NULL) goto failure;
  jmethodID method = (*env)->GetStaticMethodID(env, lease.bridge_class, "webViewCommandForRuntime",
                                               "(IJLjava/lang/String;Ljava/lang/String;J)Z");
  exception_free = ajni_check_exception(env, "NativeBridge.webViewCommandForRuntime lookup");
  if (!exception_free || method == NULL) goto failure;
  jboolean queued = (*env)->CallStaticBooleanMethod(env, lease.bridge_class, method, command,
                                                     (jlong)handle, text, request, (jlong)lease.runtime_generation);
  exception_free = ajni_check_exception(env, "NativeBridge.webViewCommandForRuntime");
  if (!exception_free) goto failure;
  status = queued == JNI_TRUE ? AJNI_OK : ajni_set_error(AJNI_NATIVE_FAILURE, "WebView command was rejected");
  goto cleanup;
failure:
  status = ajni_error_status();
  if (status == AJNI_OK) status = ajni_set_error(AJNI_NATIVE_FAILURE, "Could not prepare WebView command");
cleanup:
  if (text != NULL) (*env)->DeleteLocalRef(env, text);
  if (request != NULL) (*env)->DeleteLocalRef(env, request);
  ajni_bridge_lease_release(&lease);
  return status;
}

MOONBIT_FFI_EXPORT int32_t ajni_webview_set_bounds(int64_t handle, int32_t x, int32_t y,
                                                    int32_t width, int32_t height) {
  ajni_reset_error();
  AjniBridgeLease lease;
  int32_t status = ajni_bridge_lease_acquire(&lease);
  if (status != AJNI_OK) return status;
  JNIEnv *env = lease.env;
  jmethodID method = (*env)->GetStaticMethodID(env, lease.bridge_class, "webViewSetBoundsForRuntime", "(JIIIIJ)Z");
  bool exception_free = ajni_check_exception(env, "NativeBridge.webViewSetBoundsForRuntime lookup");
  if (!exception_free) {
    status = AJNI_JAVA_EXCEPTION;
  } else if (method == NULL) {
    status = ajni_set_error(AJNI_NATIVE_FAILURE, "WebView bounds method was not found");
  } else {
    jboolean queued = (*env)->CallStaticBooleanMethod(env, lease.bridge_class, method,
                                                       (jlong)handle, x, y, width, height, (jlong)lease.runtime_generation);
    exception_free = ajni_check_exception(env, "NativeBridge.webViewSetBoundsForRuntime");
    status = !exception_free ? AJNI_JAVA_EXCEPTION : queued == JNI_TRUE ? AJNI_OK :
        ajni_set_error(AJNI_NATIVE_FAILURE, "WebView bounds command was rejected");
  }
  ajni_bridge_lease_release(&lease);
  return status;
}

#else

MOONBIT_FFI_EXPORT int32_t ajni_webview_command(int32_t command, int64_t handle,
                                                 moonbit_bytes_t payload,
                                                 moonbit_bytes_t request_id) {
  (void)command;
  (void)handle;
  (void)payload;
  (void)request_id;
  return ajni_set_error(AJNI_UNSUPPORTED, "Android WebView commands are unsupported on this platform");
}

MOONBIT_FFI_EXPORT int32_t ajni_webview_set_bounds(int64_t handle, int32_t x, int32_t y,
                                                    int32_t width, int32_t height) {
  (void)handle;
  (void)x;
  (void)y;
  (void)width;
  (void)height;
  return ajni_set_error(AJNI_UNSUPPORTED, "Android WebView bounds are unsupported on this platform");
}

#endif
