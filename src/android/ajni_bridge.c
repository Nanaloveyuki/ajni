#include <stdint.h>

#if defined(AJNI_STANDALONE_JNI)
#define MOONBIT_FFI_EXPORT
typedef uint8_t *moonbit_bytes_t;
static void moonbit_incref(void *value) { (void)value; }
static void moonbit_decref(void *value) { (void)value; }
#else
#include <moonbit.h>
#endif

#include <stdio.h>
#include <string.h>
#include "ajni_android_internal.h"

static _Thread_local int32_t g_error_status = AJNI_OK;
static _Thread_local char g_error_message[1024];

void ajni_reset_error(void) {
  g_error_status = AJNI_OK;
  g_error_message[0] = '\0';
}

int32_t ajni_set_error(int32_t status, const char *message) {
  g_error_status = status;
  snprintf(g_error_message, sizeof(g_error_message), "%s", message);
  return status;
}

int32_t ajni_error_status(void) { return g_error_status; }

#if !defined(AJNI_STANDALONE_JNI)
MOONBIT_FFI_EXPORT moonbit_bytes_t ajni_last_error(void) {
  size_t length = strlen(g_error_message);
  moonbit_bytes_t bytes = moonbit_make_bytes((int32_t)length, 0);
  if (length != 0) memcpy(bytes, g_error_message, length);
  return bytes;
}
#endif

#if defined(__ANDROID__)
#include <stdbool.h>
#include <stddef.h>
#include <pthread.h>
#endif

typedef void (*ajni_event_callback)(void *, int32_t, int32_t, int32_t);

#if defined(AJNI_USE_MOONBIT_EXPORTS)
extern int32_t ajni_dispatch_event(int32_t kind, int32_t first, int32_t second);
extern void moonbit_runtime_init(int argc, char **argv);
extern void moonbit_init(void);
#endif

static ajni_event_callback g_event_callback = NULL;
static void *g_event_context = NULL;

#if defined(__ANDROID__)
static pthread_mutex_t g_event_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_event_idle = PTHREAD_COND_INITIALIZER;
static size_t g_active_event_callbacks = 0;
static bool g_event_shutting_down = false;

static void ajni_event_callback_finished(void) {
  pthread_mutex_lock(&g_event_lock);
  --g_active_event_callbacks;
  pthread_cond_broadcast(&g_event_idle);
  pthread_mutex_unlock(&g_event_lock);
}
#endif

static void ajni_emit(int32_t kind, int32_t first, int32_t second) {
#if defined(AJNI_USE_MOONBIT_EXPORTS)
#if defined(__ANDROID__)
  pthread_mutex_lock(&g_event_lock);
  if (g_event_shutting_down) {
    pthread_mutex_unlock(&g_event_lock);
    return;
  }
  ++g_active_event_callbacks;
  pthread_mutex_unlock(&g_event_lock);
#endif
  ajni_dispatch_event(kind, first, second);
#if defined(__ANDROID__)
  ajni_event_callback_finished();
#endif
#else
#if defined(__ANDROID__)
  ajni_event_callback callback = NULL;
  void *context = NULL;
  pthread_mutex_lock(&g_event_lock);
  if (!g_event_shutting_down) {
    callback = g_event_callback;
    context = g_event_context;
    if (callback != NULL) {
      if (context != NULL) moonbit_incref(context);
      ++g_active_event_callbacks;
    }
  }
  pthread_mutex_unlock(&g_event_lock);
  if (callback != NULL) {
    callback(context, kind, first, second);
    if (context != NULL) moonbit_decref(context);
    ajni_event_callback_finished();
  }
#else
  if (g_event_callback != NULL) g_event_callback(g_event_context, kind, first, second);
#endif
#endif
}

MOONBIT_FFI_EXPORT void ajni_install_event_callback(
    ajni_event_callback callback, void *context) {
#if defined(__ANDROID__)
  void *old_context = NULL;
  pthread_mutex_lock(&g_event_lock);
  old_context = g_event_context;
  g_event_callback = callback;
  g_event_context = context;
  g_event_shutting_down = false;
  pthread_mutex_unlock(&g_event_lock);
  if (old_context != NULL) {
    moonbit_decref(old_context);
  }
#else
  if (g_event_context != NULL) {
    moonbit_decref(g_event_context);
  }
  g_event_callback = callback;
  g_event_context = context;
#endif
}

MOONBIT_FFI_EXPORT void ajni_emit_for_test(
    int32_t kind, int32_t first, int32_t second) {
  ajni_emit(kind, first, second);
}

#if defined(__ANDROID__)

#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include "android_compat.h"
#include <errno.h>
#include <fcntl.h>
#include <jni.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define AJNI_LOG_TAG "ajni"
#define AJNI_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, AJNI_LOG_TAG, __VA_ARGS__)
#define AJNI_LOGI(...) __android_log_print(ANDROID_LOG_INFO, AJNI_LOG_TAG, __VA_ARGS__)

enum {
  AJNI_CREATED = 1,
  AJNI_STARTED = 2,
  AJNI_RESUMED = 3,
  AJNI_PAUSED = 4,
  AJNI_STOPPED = 5,
  AJNI_DESTROYED = 6,
  AJNI_SURFACE_CREATED = 10,
  AJNI_SURFACE_CHANGED = 11,
  AJNI_SURFACE_DESTROYED = 12,
  AJNI_WORKER_ATTACHED = 20,
  AJNI_UI_TASK = 21,
};

JavaVM *g_vm = NULL;
jclass g_bridge_class = NULL;
static ANativeWindow *g_window = NULL;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_idle = PTHREAD_COND_INITIALIZER;
bool g_initialized = false;
static bool g_was_initialized = false;
static int64_t g_runtime_generation = 0;
static size_t g_active_workers = 0;
static size_t g_active_bridge_calls = 0;
#if defined(AJNI_USE_MOONBIT_EXPORTS)
static bool g_moonbit_initialized = false;

static void ajni_initialize_moonbit(void) {
  if (!g_moonbit_initialized) {
    moonbit_runtime_init(0, NULL);
    moonbit_init();
    g_moonbit_initialized = true;
  }
}
#endif

#if __ANDROID_API__ < 28
int getentropy(void *buffer, size_t length) {
  if (length > 256) {
    errno = EIO;
    return -1;
  }
  int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
  if (fd < 0) return -1;
  unsigned char *output = (unsigned char *)buffer;
  size_t offset = 0;
  while (offset < length) {
    ssize_t read_count = read(fd, output + offset, length - offset);
    if (read_count <= 0) {
      int saved_errno = errno;
      close(fd);
      errno = saved_errno;
      return -1;
    }
    offset += (size_t)read_count;
  }
  close(fd);
  return 0;
}
#endif

void ajni_throw_new(JNIEnv *env, const char *class_name, const char *message) {
  if ((*env)->ExceptionCheck(env)) return;
  jclass exception_class = (*env)->FindClass(env, class_name);
  if (exception_class == NULL) return;
  if (!(*env)->ExceptionCheck(env)) (*env)->ThrowNew(env, exception_class, message);
  (*env)->DeleteLocalRef(env, exception_class);
}

bool ajni_check_exception(JNIEnv *env, const char *operation) {
  if (!(*env)->ExceptionCheck(env)) return true;
  jthrowable exception = (*env)->ExceptionOccurred(env);
  (*env)->ExceptionClear(env);
  ajni_set_error(AJNI_JAVA_EXCEPTION, operation);
  jclass exception_class = NULL;
  jstring description = NULL;
  char *message = NULL;
  if (exception != NULL) {
    exception_class = (*env)->GetObjectClass(env, exception);
    if (!(*env)->ExceptionCheck(env) && exception_class != NULL) {
      jmethodID to_string = (*env)->GetMethodID(env, exception_class, "toString", "()Ljava/lang/String;");
      if (!(*env)->ExceptionCheck(env) && to_string != NULL) {
        description = (jstring)(*env)->CallObjectMethod(env, exception, to_string);
        if (!(*env)->ExceptionCheck(env) && description != NULL) {
          size_t length = 0;
          message = ajni_utf8_from_string(env, description, &length);
          if (message != NULL && !(*env)->ExceptionCheck(env)) {
            snprintf(g_error_message, sizeof(g_error_message), "%s: %.*s", operation,
                     (int)(length < sizeof(g_error_message) ? length : sizeof(g_error_message) - 1), message);
          }
        }
      }
    }
  }
  if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
  free(message);
  if (description != NULL) (*env)->DeleteLocalRef(env, description);
  if (exception_class != NULL) (*env)->DeleteLocalRef(env, exception_class);
  if (exception != NULL) (*env)->DeleteLocalRef(env, exception);
  g_error_status = AJNI_JAVA_EXCEPTION;
  AJNI_LOGE("%s", g_error_message);
  return false;
}

static void ajni_release_window_locked(void) {
  if (g_window != NULL) {
    ANativeWindow_release(g_window);
    g_window = NULL;
  }
}

int32_t ajni_bridge_lease_acquire(AjniBridgeLease *lease) {
  memset(lease, 0, sizeof(*lease));
  pthread_mutex_lock(&g_lock);
  if (!g_initialized || g_vm == NULL || g_bridge_class == NULL) {
    int32_t status = g_was_initialized ? AJNI_RUNTIME_DESTROYED : AJNI_NOT_INITIALIZED;
    pthread_mutex_unlock(&g_lock);
    return ajni_set_error(status, "Android runtime is not ready");
  }
  lease->vm = g_vm;
  lease->runtime_generation = g_runtime_generation;
  ++g_active_bridge_calls;
  pthread_mutex_unlock(&g_lock);

  jint result = (*lease->vm)->GetEnv(lease->vm, (void **)&lease->env, JNI_VERSION_1_6);
  if (result == JNI_EDETACHED) {
    result = (*lease->vm)->AttachCurrentThread(lease->vm, &lease->env, NULL);
    if (result == JNI_OK) lease->attached = true;
  }
  if (result != JNI_OK || lease->env == NULL) {
    ajni_set_error(AJNI_NATIVE_FAILURE, "Could not attach current thread to JavaVM");
    ajni_bridge_lease_release(lease);
    return AJNI_NATIVE_FAILURE;
  }
  if ((*lease->env)->ExceptionCheck(lease->env)) {
    ajni_set_error(AJNI_JAVA_EXCEPTION, "JNI entry already has a pending Java exception");
    ajni_bridge_lease_release(lease);
    return AJNI_JAVA_EXCEPTION;
  }
  pthread_mutex_lock(&g_lock);
  lease->bridge_class = (jclass)(*lease->env)->NewLocalRef(lease->env, g_bridge_class);
  pthread_mutex_unlock(&g_lock);
  bool exception_free = ajni_check_exception(lease->env, "NewLocalRef(NativeBridge)");
  if (!exception_free || lease->bridge_class == NULL) {
    if (exception_free) ajni_set_error(AJNI_NATIVE_FAILURE, "Could not retain NativeBridge class");
    int32_t status = ajni_error_status();
    ajni_bridge_lease_release(lease);
    return status;
  }
  return AJNI_OK;
}

void ajni_bridge_lease_release(AjniBridgeLease *lease) {
  if (lease->bridge_class != NULL && lease->env != NULL) {
    (*lease->env)->DeleteLocalRef(lease->env, lease->bridge_class);
  }
  if (lease->attached && lease->vm != NULL) {
    (*lease->vm)->DetachCurrentThread(lease->vm);
  }
  if (lease->vm != NULL) {
    pthread_mutex_lock(&g_lock);
    --g_active_bridge_calls;
    pthread_cond_broadcast(&g_idle);
    pthread_mutex_unlock(&g_lock);
  }
  memset(lease, 0, sizeof(*lease));
}

static int32_t ajni_enqueue_ui_callback(AjniBridgeLease *lease, jint event_kind) {
  jmethodID post = (*lease->env)->GetStaticMethodID(lease->env, lease->bridge_class,
                                                   "postUiCallbackForRuntime", "(IJ)Z");
  bool exception_free = ajni_check_exception(lease->env, "NativeBridge.postUiCallbackForRuntime lookup");
  if (!exception_free) return AJNI_JAVA_EXCEPTION;
  if (post == NULL) return ajni_set_error(AJNI_NATIVE_FAILURE, "UI callback method was not found");
  jboolean queued = (*lease->env)->CallStaticBooleanMethod(lease->env, lease->bridge_class,
                                                          post, event_kind, (jlong)lease->runtime_generation);
  exception_free = ajni_check_exception(lease->env, "NativeBridge.postUiCallbackForRuntime");
  if (!exception_free) return AJNI_JAVA_EXCEPTION;
  if (queued != JNI_TRUE) return ajni_set_error(AJNI_NATIVE_FAILURE, "UI callback was rejected");
  return AJNI_OK;
}

static void ajni_draw_window_locked(uint32_t color) {
  if (g_window == NULL) {
    return;
  }
  ANativeWindow_Buffer buffer;
  int lock_result = ANativeWindow_lock(g_window, &buffer, NULL);
  if (lock_result != 0) {
    AJNI_LOGE("ANativeWindow_lock failed: %d", lock_result);
    return;
  }
  AJNI_LOGI("drawing Surface format=%d size=%dx%d stride=%d", buffer.format, buffer.width,
            buffer.height, buffer.stride);
  if (buffer.format == WINDOW_FORMAT_RGBA_8888 || buffer.format == WINDOW_FORMAT_RGBX_8888) {
    for (int y = 0; y < buffer.height; ++y) {
      uint32_t *line = (uint32_t *)buffer.bits + y * buffer.stride;
      for (int x = 0; x < buffer.width; ++x) {
        line[x] = color;
      }
    }
  } else if (buffer.format == WINDOW_FORMAT_RGB_565) {
    uint16_t red = (uint16_t)((color >> 19) & 0x1F);
    uint16_t green = (uint16_t)((color >> 10) & 0x3F);
    uint16_t blue = (uint16_t)((color >> 3) & 0x1F);
    uint16_t color565 = (uint16_t)((red << 11) | (green << 5) | blue);
    for (int y = 0; y < buffer.height; ++y) {
      uint16_t *line = (uint16_t *)buffer.bits + y * buffer.stride;
      for (int x = 0; x < buffer.width; ++x) {
        line[x] = color565;
      }
    }
  } else {
    AJNI_LOGE("unsupported Surface format %d", buffer.format);
  }
  ANativeWindow_unlockAndPost(g_window);
}

jstring ajni_string_from_utf8(JNIEnv *env, const char *text, size_t length) {
  if (env == NULL || (text == NULL && length != 0) || length > INT32_MAX ||
      length > (SIZE_MAX / sizeof(jchar)) - 1) {
    ajni_set_error(AJNI_NATIVE_FAILURE, "Invalid UTF-8 buffer length");
    return NULL;
  }
  jchar *utf16 = (jchar *)calloc(length + 1, sizeof(jchar));
  if (utf16 == NULL) {
    ajni_set_error(AJNI_NATIVE_FAILURE, "Could not allocate UTF-16 buffer");
    return NULL;
  }
  size_t out = 0;
  for (size_t i = 0; i < length;) {
    unsigned char byte = (unsigned char)text[i++];
    uint32_t codepoint = 0xFFFD;
    if (byte < 0x80) {
      codepoint = byte;
    } else if (byte >= 0xC2 && byte <= 0xDF && i < length && ((unsigned char)text[i] & 0xC0) == 0x80) {
      codepoint = ((byte & 0x1F) << 6) | ((unsigned char)text[i++] & 0x3F);
    } else if (byte >= 0xE0 && byte <= 0xEF && length - i >= 2 &&
               ((unsigned char)text[i] & 0xC0) == 0x80 && ((unsigned char)text[i + 1] & 0xC0) == 0x80 &&
               (byte != 0xE0 || (unsigned char)text[i] >= 0xA0) &&
               (byte != 0xED || (unsigned char)text[i] < 0xA0)) {
      codepoint = ((byte & 0x0F) << 12) | (((unsigned char)text[i] & 0x3F) << 6) |
                  ((unsigned char)text[i + 1] & 0x3F);
      i += 2;
    } else if (byte >= 0xF0 && byte <= 0xF4 && length - i >= 3 &&
               ((unsigned char)text[i] & 0xC0) == 0x80 && ((unsigned char)text[i + 1] & 0xC0) == 0x80 &&
               ((unsigned char)text[i + 2] & 0xC0) == 0x80 &&
               (byte != 0xF0 || (unsigned char)text[i] >= 0x90) &&
               (byte != 0xF4 || (unsigned char)text[i] < 0x90)) {
      codepoint = ((byte & 0x07) << 18) | (((unsigned char)text[i] & 0x3F) << 12) |
                  (((unsigned char)text[i + 1] & 0x3F) << 6) | ((unsigned char)text[i + 2] & 0x3F);
      i += 3;
    }
    if (codepoint <= 0xFFFF) {
      utf16[out++] = (jchar)((codepoint >= 0xD800 && codepoint <= 0xDFFF) ? 0xFFFD : codepoint);
    } else if (codepoint <= 0x10FFFF) {
      codepoint -= 0x10000;
      utf16[out++] = (jchar)(0xD800 | (codepoint >> 10));
      utf16[out++] = (jchar)(0xDC00 | (codepoint & 0x3FF));
    } else {
      utf16[out++] = 0xFFFD;
    }
  }
  jstring result = (*env)->NewString(env, utf16, (jsize)out);
  free(utf16);
  return result;
}

char *ajni_utf8_from_string(JNIEnv *env, jstring text, size_t *out_length) {
  if (env == NULL || text == NULL || out_length == NULL) {
    return NULL;
  }
  *out_length = 0;
  jsize length = (*env)->GetStringLength(env, text);
  if ((*env)->ExceptionCheck(env)) return NULL;
  if (length < 0 || (size_t)length > (SIZE_MAX - 1) / 3) {
    return NULL;
  }
  const jchar *chars = (*env)->GetStringChars(env, text, NULL);
  if ((*env)->ExceptionCheck(env) || chars == NULL) {
    if (chars != NULL) (*env)->ReleaseStringChars(env, text, chars);
    return NULL;
  }
  char *output = (char *)malloc((size_t)length * 3 + 1);
  if (output == NULL) {
    ajni_set_error(AJNI_NATIVE_FAILURE, "Could not allocate UTF-8 buffer");
    (*env)->ReleaseStringChars(env, text, chars);
    return NULL;
  }
  size_t out = 0;
  for (jsize index = 0; index < length; ++index) {
    uint32_t codepoint = chars[index];
    if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
      if (index + 1 < length && chars[index + 1] >= 0xDC00 && chars[index + 1] <= 0xDFFF) {
        codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (chars[++index] - 0xDC00);
      } else {
        codepoint = 0xFFFD;
      }
    } else if (codepoint >= 0xDC00 && codepoint <= 0xDFFF) {
      codepoint = 0xFFFD;
    }
    if (codepoint < 0x80) {
      output[out++] = (char)codepoint;
    } else if (codepoint < 0x800) {
      output[out++] = (char)(0xC0 | (codepoint >> 6));
      output[out++] = (char)(0x80 | (codepoint & 0x3F));
    } else if (codepoint < 0x10000) {
      output[out++] = (char)(0xE0 | (codepoint >> 12));
      output[out++] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
      output[out++] = (char)(0x80 | (codepoint & 0x3F));
    } else {
      output[out++] = (char)(0xF0 | (codepoint >> 18));
      output[out++] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
      output[out++] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
      output[out++] = (char)(0x80 | (codepoint & 0x3F));
    }
  }
  (*env)->ReleaseStringChars(env, text, chars);
  *out_length = out;
  return output;
}

static jstring ajni_native_echo(JNIEnv *env, jclass unused, jstring text) {
  (void)unused;
  if (text == NULL) {
    return NULL;
  }
  size_t length = 0;
  char *utf8 = ajni_utf8_from_string(env, text, &length);
  if (utf8 == NULL) {
    ajni_throw_new(env, "java/lang/OutOfMemoryError", "Could not convert Java string");
    return NULL;
  }
  jstring result = ajni_string_from_utf8(env, utf8, length);
  free(utf8);
  if (result == NULL) ajni_throw_new(env, "java/lang/OutOfMemoryError", "Could not allocate Java string");
  return result;
}

static void ajni_native_initialize(JNIEnv *env, jclass unused, jobject context, jlong generation) {
  (void)unused;
  if ((*env)->ExceptionCheck(env)) return;
  if (context == NULL || generation <= 0) {
    ajni_throw_new(env, "java/lang/IllegalArgumentException", "context and runtime generation must be valid");
    return;
  }
  pthread_mutex_lock(&g_lock);
  g_runtime_generation = generation;
  g_was_initialized = true;
  g_initialized = true;
  pthread_mutex_unlock(&g_lock);
}

static void ajni_native_shutdown(JNIEnv *env, jclass unused) {
  (void)env;
  (void)unused;
  pthread_mutex_lock(&g_lock);
  ajni_release_window_locked();
  g_initialized = false;
  g_runtime_generation = 0;
  pthread_mutex_unlock(&g_lock);
}

static void ajni_native_lifecycle(JNIEnv *env, jclass unused, jint state) {
  (void)env;
  (void)unused;
  ajni_emit(state, 0, 0);
}

static void ajni_native_surface_created(JNIEnv *env, jclass unused, jobject surface, jint width, jint height) {
  (void)unused;
  if (surface == NULL) return;
  ANativeWindow *window = ANativeWindow_fromSurface(env, surface);
  if ((*env)->ExceptionCheck(env) || window == NULL) {
    if (window != NULL) ANativeWindow_release(window);
    ajni_throw_new(env, "java/lang/IllegalStateException", "ANativeWindow_fromSurface failed");
    return;
  }
  pthread_mutex_lock(&g_lock);
  ajni_release_window_locked();
  int geometry_result = ANativeWindow_setBuffersGeometry(window, 0, 0, WINDOW_FORMAT_RGBA_8888);
  if (geometry_result != 0) {
    AJNI_LOGE("ANativeWindow_setBuffersGeometry failed: %d", geometry_result);
  }
  g_window = window;
  ajni_draw_window_locked(0xFFE8731A);
  pthread_mutex_unlock(&g_lock);
  ajni_emit(AJNI_SURFACE_CREATED, width, height);
}

static void ajni_native_surface_changed(JNIEnv *env, jclass unused, jint width, jint height) {
  (void)env;
  (void)unused;
  pthread_mutex_lock(&g_lock);
  ajni_draw_window_locked(0xFF7B8900);
  pthread_mutex_unlock(&g_lock);
  ajni_emit(AJNI_SURFACE_CHANGED, width, height);
}

static void ajni_native_surface_destroyed(JNIEnv *env, jclass unused) {
  (void)env;
  (void)unused;
  pthread_mutex_lock(&g_lock);
  ajni_release_window_locked();
  pthread_mutex_unlock(&g_lock);
  ajni_emit(AJNI_SURFACE_DESTROYED, 0, 0);
}

static void ajni_native_on_ui_task(JNIEnv *env, jclass unused, jint event_kind) {
  (void)env;
  (void)unused;
  pthread_mutex_lock(&g_lock);
  bool initialized = g_initialized;
  pthread_mutex_unlock(&g_lock);
  if (!initialized) return;
  AJNI_LOGI("UI callback dispatched on Android main thread");
  if (event_kind == AJNI_WORKER_ATTACHED) ajni_emit(AJNI_WORKER_ATTACHED, 0, 0);
  ajni_emit(AJNI_UI_TASK, 0, 0);
}

static void *ajni_worker_main(void *context) {
  int64_t generation = *(int64_t *)context;
  free(context);
  ajni_reset_error();
  AjniBridgeLease lease;
  if (ajni_bridge_lease_acquire(&lease) == AJNI_OK) {
    if (lease.runtime_generation == generation &&
        ajni_enqueue_ui_callback(&lease, AJNI_WORKER_ATTACHED) == AJNI_OK) {
      AJNI_LOGI("native worker posted UI callback");
    }
    ajni_bridge_lease_release(&lease);
  }
  pthread_mutex_lock(&g_lock);
  --g_active_workers;
  pthread_cond_broadcast(&g_idle);
  pthread_mutex_unlock(&g_lock);
  return NULL;
}

static int32_t ajni_start_worker_locked(void) {
  if (!g_initialized || g_vm == NULL || g_bridge_class == NULL) {
    return ajni_set_error(g_was_initialized ? AJNI_RUNTIME_DESTROYED : AJNI_NOT_INITIALIZED,
                          "Android runtime is not ready");
  }
  int64_t *generation = malloc(sizeof(*generation));
  if (generation == NULL) return ajni_set_error(AJNI_NATIVE_FAILURE, "Could not allocate worker context");
  *generation = g_runtime_generation;
  pthread_attr_t attributes;
  int result = pthread_attr_init(&attributes);
  if (result != 0) {
    free(generation);
    return ajni_set_error(AJNI_NATIVE_FAILURE, "Could not initialize worker attributes");
  }
  result = pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
  if (result == 0) {
    ++g_active_workers;
    pthread_t thread;
    result = pthread_create(&thread, &attributes, ajni_worker_main, generation);
    if (result != 0) --g_active_workers;
  }
  pthread_attr_destroy(&attributes);
  if (result != 0) {
    free(generation);
    return ajni_set_error(AJNI_NATIVE_FAILURE, "Could not create native worker");
  }
  return AJNI_OK;
}

static void ajni_native_start_worker(JNIEnv *env, jclass unused) {
  (void)unused;
  ajni_reset_error();
  pthread_mutex_lock(&g_lock);
  int32_t status = ajni_start_worker_locked();
  pthread_mutex_unlock(&g_lock);
  if (status != AJNI_OK) ajni_throw_new(env, "java/lang/IllegalStateException", g_error_message);
}

MOONBIT_FFI_EXPORT int32_t ajni_runtime_ready(void) {
  pthread_mutex_lock(&g_lock);
  int32_t ready = g_initialized ? 1 : 0;
  pthread_mutex_unlock(&g_lock);
  return ready;
}
MOONBIT_FFI_EXPORT int32_t ajni_post_ui_callback(void) {
  ajni_reset_error();
  AjniBridgeLease lease;
  int32_t status = ajni_bridge_lease_acquire(&lease);
  if (status != AJNI_OK) return status;
  status = ajni_enqueue_ui_callback(&lease, AJNI_UI_TASK);
  ajni_bridge_lease_release(&lease);
  return status;
}
MOONBIT_FFI_EXPORT int32_t ajni_start_worker(void) {
  ajni_reset_error();
  pthread_mutex_lock(&g_lock);
  int32_t status = ajni_start_worker_locked();
  pthread_mutex_unlock(&g_lock);
  return status;
}

JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void *unused) {
  (void)unused;
  pthread_mutex_lock(&g_lock);
  g_vm = vm;
  pthread_mutex_unlock(&g_lock);
  JNIEnv *env = NULL;
  if ((*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) != JNI_OK) return JNI_ERR;
#if defined(AJNI_USE_MOONBIT_EXPORTS)
  ajni_initialize_moonbit();
#endif
  jclass local_class = (*env)->FindClass(env, "dev/nanaloveyuki/ajni/host/NativeBridge");
  if ((*env)->ExceptionCheck(env) || local_class == NULL) return JNI_ERR;
  const JNINativeMethod methods[] = {
      {"nativeInitialize", "(Landroid/content/Context;J)V", (void *)ajni_native_initialize},
      {"nativeShutdown", "()V", (void *)ajni_native_shutdown},
      {"nativeLifecycle", "(I)V", (void *)ajni_native_lifecycle},
      {"nativeSurfaceCreated", "(Landroid/view/Surface;II)V", (void *)ajni_native_surface_created},
      {"nativeSurfaceChanged", "(II)V", (void *)ajni_native_surface_changed},
      {"nativeSurfaceDestroyed", "()V", (void *)ajni_native_surface_destroyed},
      {"nativeOnUiTask", "(I)V", (void *)ajni_native_on_ui_task},
      {"nativeStartWorker", "()V", (void *)ajni_native_start_worker},
      {"nativeEcho", "(Ljava/lang/String;)Ljava/lang/String;", (void *)ajni_native_echo},
  };
  jint registration = (*env)->RegisterNatives(env, local_class, methods, sizeof(methods) / sizeof(methods[0]));
  if ((*env)->ExceptionCheck(env) || registration != JNI_OK) {
    (*env)->DeleteLocalRef(env, local_class);
    return JNI_ERR;
  }
#if defined(AJNI_FEATURE_WEBVIEW)
  if (!ajni_register_webview_natives(env, local_class)) {
    (*env)->DeleteLocalRef(env, local_class);
    return JNI_ERR;
  }
#endif
  jclass bridge_class = (jclass)(*env)->NewGlobalRef(env, local_class);
  (*env)->DeleteLocalRef(env, local_class);
  if ((*env)->ExceptionCheck(env) || bridge_class == NULL) return JNI_ERR;
  pthread_mutex_lock(&g_lock);
  g_bridge_class = bridge_class;
  pthread_mutex_unlock(&g_lock);
  pthread_mutex_lock(&g_event_lock);
  g_event_shutting_down = false;
  pthread_mutex_unlock(&g_event_lock);
  return JNI_VERSION_1_6;
}

JNIEXPORT void JNI_OnUnload(JavaVM *vm, void *unused) {
  (void)unused;
  JNIEnv *env = NULL;
  if ((*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) != JNI_OK) return;
  ajni_native_shutdown(env, NULL);
  pthread_mutex_lock(&g_lock);
  while (g_active_workers != 0 || g_active_bridge_calls != 0) pthread_cond_wait(&g_idle, &g_lock);
  jclass bridge_class = g_bridge_class;
  g_bridge_class = NULL;
  g_vm = NULL;
  pthread_mutex_unlock(&g_lock);
  if (bridge_class != NULL) (*env)->DeleteGlobalRef(env, bridge_class);
  pthread_mutex_lock(&g_event_lock);
  g_event_shutting_down = true;
  g_event_callback = NULL;
  while (g_active_event_callbacks != 0) {
    pthread_cond_wait(&g_event_idle, &g_event_lock);
  }
  void *event_context = g_event_context;
  g_event_context = NULL;
  pthread_mutex_unlock(&g_event_lock);
  if (event_context != NULL) moonbit_decref(event_context);
}

#else

MOONBIT_FFI_EXPORT int32_t ajni_runtime_ready(void) { return 0; }
MOONBIT_FFI_EXPORT int32_t ajni_post_ui_callback(void) {
  return ajni_set_error(AJNI_UNSUPPORTED, "Android UI callbacks are unsupported on this platform");
}
MOONBIT_FFI_EXPORT int32_t ajni_start_worker(void) {
  return ajni_set_error(AJNI_UNSUPPORTED, "Android workers are unsupported on this platform");
}

#endif
