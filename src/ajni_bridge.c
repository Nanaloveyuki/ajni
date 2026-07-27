#if defined(AJNI_STANDALONE_JNI)
#define MOONBIT_FFI_EXPORT
static void moonbit_decref(void *value) { (void)value; }
#else
#include <moonbit.h>
#endif
#include <stdint.h>

typedef void (*ajni_event_callback)(void *, int32_t, int32_t, int32_t);

#if defined(AJNI_USE_MOONBIT_EXPORTS)
extern int32_t ajni_dispatch_event(int32_t kind, int32_t first, int32_t second);
extern void moonbit_runtime_init(int argc, char **argv);
extern void moonbit_init(void);
#endif

static ajni_event_callback g_event_callback = NULL;
static void *g_event_context = NULL;

static void ajni_emit(int32_t kind, int32_t first, int32_t second) {
#if defined(AJNI_USE_MOONBIT_EXPORTS)
  ajni_dispatch_event(kind, first, second);
#else
  if (g_event_callback != NULL) {
    g_event_callback(g_event_context, kind, first, second);
  }
#endif
}

MOONBIT_FFI_EXPORT void ajni_install_event_callback(
    ajni_event_callback callback, void *context) {
  if (g_event_context != NULL) {
    moonbit_decref(g_event_context);
  }
  g_event_callback = callback;
  g_event_context = context;
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

static JavaVM *g_vm = NULL;
static jclass g_bridge_class = NULL;
static jobject g_class_loader = NULL;
static ANativeWindow *g_window = NULL;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static bool g_initialized = false;
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

static bool ajni_check_exception(JNIEnv *env, const char *operation) {
  if (!(*env)->ExceptionCheck(env)) {
    return true;
  }
  (*env)->ExceptionDescribe(env);
  (*env)->ExceptionClear(env);
  AJNI_LOGE("Java exception during %s", operation);
  return false;
}

static void ajni_release_window_locked(void) {
  if (g_window != NULL) {
    ANativeWindow_release(g_window);
    g_window = NULL;
  }
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

static jstring ajni_string_from_utf8(JNIEnv *env, const char *text, size_t length) {
  jchar *utf16 = (jchar *)calloc(length + 1, sizeof(jchar));
  if (utf16 == NULL) {
    return NULL;
  }
  size_t out = 0;
  for (size_t i = 0; i < length;) {
    unsigned char byte = (unsigned char)text[i++];
    uint32_t codepoint = 0xFFFD;
    if (byte < 0x80) {
      codepoint = byte;
    } else if ((byte & 0xE0) == 0xC0 && i < length && ((unsigned char)text[i] & 0xC0) == 0x80) {
      codepoint = ((byte & 0x1F) << 6) | ((unsigned char)text[i++] & 0x3F);
    } else if ((byte & 0xF0) == 0xE0 && i + 1 < length &&
               ((unsigned char)text[i] & 0xC0) == 0x80 && ((unsigned char)text[i + 1] & 0xC0) == 0x80) {
      codepoint = ((byte & 0x0F) << 12) | (((unsigned char)text[i] & 0x3F) << 6) |
                  ((unsigned char)text[i + 1] & 0x3F);
      i += 2;
    } else if ((byte & 0xF8) == 0xF0 && i + 2 < length &&
               ((unsigned char)text[i] & 0xC0) == 0x80 && ((unsigned char)text[i + 1] & 0xC0) == 0x80 &&
               ((unsigned char)text[i + 2] & 0xC0) == 0x80) {
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

static char *ajni_utf8_from_string(JNIEnv *env, jstring text, size_t *out_length) {
  const jchar *chars = (*env)->GetStringChars(env, text, NULL);
  if (chars == NULL || !ajni_check_exception(env, "GetStringChars")) {
    return NULL;
  }
  jsize length = (*env)->GetStringLength(env, text);
  char *output = (char *)malloc((size_t)length * 3 + 1);
  if (output == NULL) {
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
  if (utf8 == NULL) return NULL;
  jstring result = ajni_string_from_utf8(env, utf8, length);
  free(utf8);
  ajni_check_exception(env, "NewString");
  return result;
}

static void ajni_native_initialize(JNIEnv *env, jclass unused, jobject context) {
  (void)unused;
  if (context == NULL) {
    (*env)->ThrowNew(env, "java/lang/IllegalArgumentException", "context must not be null");
    return;
  }
  jclass context_class = (*env)->GetObjectClass(env, context);
  jmethodID get_loader = (*env)->GetMethodID(env, context_class, "getClassLoader", "()Ljava/lang/ClassLoader;");
  jobject loader = get_loader == NULL ? NULL : (*env)->CallObjectMethod(env, context, get_loader);
  if (!ajni_check_exception(env, "Context.getClassLoader")) {
    if (context_class != NULL) (*env)->DeleteLocalRef(env, context_class);
    return;
  }
  pthread_mutex_lock(&g_lock);
  if (g_class_loader != NULL) {
    (*env)->DeleteGlobalRef(env, g_class_loader);
  }
  g_class_loader = loader == NULL ? NULL : (*env)->NewGlobalRef(env, loader);
  g_initialized = g_class_loader != NULL;
  pthread_mutex_unlock(&g_lock);
  if (loader != NULL) (*env)->DeleteLocalRef(env, loader);
  if (context_class != NULL) (*env)->DeleteLocalRef(env, context_class);
  if (!g_initialized) {
    (*env)->ThrowNew(env, "java/lang/IllegalStateException", "could not retain application ClassLoader");
  }
}

static void ajni_native_shutdown(JNIEnv *env, jclass unused) {
  (void)unused;
  pthread_mutex_lock(&g_lock);
  ajni_release_window_locked();
  if (g_class_loader != NULL) {
    (*env)->DeleteGlobalRef(env, g_class_loader);
    g_class_loader = NULL;
  }
  g_initialized = false;
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
  if (window == NULL || !ajni_check_exception(env, "ANativeWindow_fromSurface")) {
    AJNI_LOGE("ANativeWindow_fromSurface failed");
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

static void ajni_native_on_ui_task(JNIEnv *env, jclass unused) {
  (void)env;
  (void)unused;
  AJNI_LOGI("UI callback dispatched on Android main thread");
  ajni_emit(AJNI_UI_TASK, 0, 0);
}

static void *ajni_worker_main(void *unused) {
  (void)unused;
  JNIEnv *env = NULL;
  if ((*g_vm)->AttachCurrentThread(g_vm, &env, NULL) != JNI_OK || env == NULL) return NULL;
  AJNI_LOGI("native worker attached to JavaVM");
  ajni_emit(AJNI_WORKER_ATTACHED, 0, 0);
  jmethodID post = (*env)->GetStaticMethodID(env, g_bridge_class, "postUiCallback", "()V");
  if (post != NULL) {
    (*env)->CallStaticVoidMethod(env, g_bridge_class, post);
    ajni_check_exception(env, "NativeBridge.postUiCallback");
    AJNI_LOGI("native worker posted UI callback");
  }
  (*g_vm)->DetachCurrentThread(g_vm);
  return NULL;
}

static void ajni_native_start_worker(JNIEnv *env, jclass unused) {
  (void)env;
  (void)unused;
  pthread_t thread;
  if (pthread_create(&thread, NULL, ajni_worker_main, NULL) == 0) {
    pthread_detach(thread);
  } else {
    (*env)->ThrowNew(env, "java/lang/IllegalStateException", "could not start native worker");
  }
}

MOONBIT_FFI_EXPORT int32_t ajni_runtime_ready(void) { return g_initialized ? 1 : 0; }
MOONBIT_FFI_EXPORT int32_t ajni_post_ui_callback(void) {
  if (!g_initialized || g_vm == NULL || g_bridge_class == NULL) return 0;
  JNIEnv *env = NULL;
  int attached = 0;
  if ((*g_vm)->GetEnv(g_vm, (void **)&env, JNI_VERSION_1_6) == JNI_EDETACHED) {
    if ((*g_vm)->AttachCurrentThread(g_vm, &env, NULL) != JNI_OK) return 0;
    attached = 1;
  }
  jmethodID post = (*env)->GetStaticMethodID(env, g_bridge_class, "postUiCallback", "()V");
  if (post != NULL) (*env)->CallStaticVoidMethod(env, g_bridge_class, post);
  int ok = post != NULL && ajni_check_exception(env, "NativeBridge.postUiCallback");
  if (attached) (*g_vm)->DetachCurrentThread(g_vm);
  return ok ? 1 : 0;
}
MOONBIT_FFI_EXPORT int32_t ajni_start_worker(void) {
  if (!g_initialized) return 0;
  pthread_t thread;
  if (pthread_create(&thread, NULL, ajni_worker_main, NULL) != 0) return 0;
  pthread_detach(thread);
  return 1;
}

JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void *unused) {
  (void)unused;
  g_vm = vm;
  JNIEnv *env = NULL;
  if ((*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) != JNI_OK) return JNI_ERR;
#if defined(AJNI_USE_MOONBIT_EXPORTS)
  ajni_initialize_moonbit();
#endif
  jclass local_class = (*env)->FindClass(env, "dev/nanaloveyuki/ajni/demo/NativeBridge");
  if (local_class == NULL || !ajni_check_exception(env, "FindClass(NativeBridge)")) return JNI_ERR;
  const JNINativeMethod methods[] = {
      {"nativeInitialize", "(Landroid/content/Context;)V", (void *)ajni_native_initialize},
      {"nativeShutdown", "()V", (void *)ajni_native_shutdown},
      {"nativeLifecycle", "(I)V", (void *)ajni_native_lifecycle},
      {"nativeSurfaceCreated", "(Landroid/view/Surface;II)V", (void *)ajni_native_surface_created},
      {"nativeSurfaceChanged", "(II)V", (void *)ajni_native_surface_changed},
      {"nativeSurfaceDestroyed", "()V", (void *)ajni_native_surface_destroyed},
      {"nativeOnUiTask", "()V", (void *)ajni_native_on_ui_task},
      {"nativeStartWorker", "()V", (void *)ajni_native_start_worker},
      {"nativeEcho", "(Ljava/lang/String;)Ljava/lang/String;", (void *)ajni_native_echo},
  };
  if ((*env)->RegisterNatives(env, local_class, methods, sizeof(methods) / sizeof(methods[0])) != JNI_OK ||
      !ajni_check_exception(env, "RegisterNatives")) {
    (*env)->DeleteLocalRef(env, local_class);
    return JNI_ERR;
  }
  g_bridge_class = (jclass)(*env)->NewGlobalRef(env, local_class);
  (*env)->DeleteLocalRef(env, local_class);
  return g_bridge_class == NULL ? JNI_ERR : JNI_VERSION_1_6;
}

JNIEXPORT void JNI_OnUnload(JavaVM *vm, void *unused) {
  (void)unused;
  JNIEnv *env = NULL;
  if ((*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) != JNI_OK) return;
  ajni_native_shutdown(env, NULL);
  if (g_bridge_class != NULL) {
    (*env)->DeleteGlobalRef(env, g_bridge_class);
    g_bridge_class = NULL;
  }
  if (g_event_context != NULL) {
    moonbit_decref(g_event_context);
    g_event_context = NULL;
  }
  g_event_callback = NULL;
  g_vm = NULL;
}

#else

MOONBIT_FFI_EXPORT int32_t ajni_runtime_ready(void) { return 1; }
MOONBIT_FFI_EXPORT int32_t ajni_post_ui_callback(void) { return 1; }
MOONBIT_FFI_EXPORT int32_t ajni_start_worker(void) { return 1; }

#endif
