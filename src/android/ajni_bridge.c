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
extern void ajni_dispatch_event(int32_t kind, int32_t first, int32_t second);
extern uint64_t ajni_create_int_callback(uint64_t env);
extern int32_t ajni_handled_observer_error(void);
extern void moonbit_runtime_init(int argc, char **argv);
extern void moonbit_init(void);
#endif

static ajni_event_callback g_event_callback = NULL;
static void *g_event_context = NULL;

static void ajni_emit(int32_t kind, int32_t first, int32_t second) {
#if defined(__ANDROID__)
  AjniEnvLease lease;
  if (ajni_rt_env_acquire(&lease) != AJNI_RT_OK) return;
  if (!ajni_android_enter((JNIEnv *)lease.env)) {
    ajni_rt_env_release(&lease);
    return;
  }
#endif
#if defined(AJNI_USE_MOONBIT_EXPORTS)
  ajni_dispatch_event(kind, first, second);
#else
  ajni_event_callback callback = g_event_callback;
  void *context = g_event_context;
  if (callback != NULL) {
    if (context != NULL) moonbit_incref(context);
    callback(context, kind, first, second);
  }
#endif
#if defined(__ANDROID__)
  ajni_android_leave((JNIEnv *)lease.env);
  ajni_rt_env_release(&lease);
#endif
}

MOONBIT_FFI_EXPORT void ajni_install_event_callback(
    ajni_event_callback callback, void *context) {
  void *old_context = g_event_context;
  g_event_callback = callback;
  g_event_context = context;
  if (old_context != NULL) moonbit_decref(old_context);
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

bool ajni_check_exception(JNIEnv *env, const char *operation) {
  if (ajni_rt_capture_exception(env, operation) == AJNI_RT_OK) return true;
  ajni_set_error(AJNI_JAVA_EXCEPTION, operation);
  AJNI_LOGE("%s", operation);
  return false;
}

bool ajni_android_enter(JNIEnv *env) {
  if (ajni_rt_host_enter(env) == AJNI_RT_OK) return true;
  ajni_rt_host_throw(env);
  return false;
}

void ajni_android_leave(JNIEnv *env) {
  (void)env;
  ajni_rt_host_leave();
}

MOONBIT_FFI_EXPORT void ajni_android_export_error(uint64_t raw_env, uint64_t raw_throwable) {
  JNIEnv *env = (JNIEnv *)(uintptr_t)raw_env;
  jthrowable throwable = (jthrowable)(uintptr_t)raw_throwable;
  if (throwable != NULL) {
    if (!(*env)->ExceptionCheck(env)) (*env)->Throw(env, throwable);
    (*env)->DeleteLocalRef(env, throwable);
  } else {
    ajni_throw_new(env, "dev/nanaloveyuki/ajni/AjniException", "Android MoonBit export failed");
  }
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
  if (!g_initialized || g_bridge_class == NULL) {
    int32_t status = g_was_initialized ? AJNI_RUNTIME_DESTROYED : AJNI_NOT_INITIALIZED;
    pthread_mutex_unlock(&g_lock);
    return ajni_set_error(status, "Android runtime is not ready");
  }
  lease->runtime_generation = g_runtime_generation;
  ++g_active_bridge_calls;
  pthread_mutex_unlock(&g_lock);

  int result = ajni_rt_env_acquire(&lease->core);
  lease->env = (JNIEnv *)lease->core.env;
  if (result != AJNI_RT_OK || lease->env == NULL) {
    int32_t status = result == AJNI_RT_CLOSED ? AJNI_RUNTIME_DESTROYED :
        result == AJNI_RT_NOT_INITIALIZED ? AJNI_NOT_INITIALIZED : AJNI_NATIVE_FAILURE;
    ajni_set_error(status, "Generic JNI session is not ready");
    ajni_bridge_lease_release(lease);
    return status;
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
  ajni_rt_env_release(&lease->core);
  if (lease->runtime_generation != 0) {
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

static jobject ajni_native_create_int_callback(JNIEnv *env, jclass unused) {
  (void)unused;
  if (!ajni_android_enter(env)) return NULL;
#if defined(AJNI_USE_MOONBIT_EXPORTS)
  jobject callback = (jobject)(uintptr_t)ajni_create_int_callback((uint64_t)(uintptr_t)env);
#else
  jobject callback = NULL;
#endif
  if (callback == NULL && !(*env)->ExceptionCheck(env))
    ajni_throw_new(env, "dev/nanaloveyuki/ajni/AjniException", "Could not create managed int callback");
  ajni_android_leave(env);
  return callback;
}

static jboolean ajni_native_handled_observer_error(JNIEnv *env, jclass unused) {
  (void)unused;
  if (!ajni_android_enter(env)) return JNI_FALSE;
#if defined(AJNI_USE_MOONBIT_EXPORTS)
  jboolean result = ajni_handled_observer_error() != 0 ? JNI_TRUE : JNI_FALSE;
#else
  jboolean result = JNI_FALSE;
#endif
  ajni_android_leave(env);
  return result;
}

static void ajni_native_initialize(JNIEnv *env, jclass unused, jobject context, jlong generation) {
  (void)unused;
  if ((*env)->ExceptionCheck(env)) return;
  if (gettid() != getpid()) {
    ajni_throw_new(env, "java/lang/IllegalStateException", "initialize must run on Android's main thread");
    return;
  }
  if (context == NULL || generation <= 0) {
    ajni_throw_new(env, "java/lang/IllegalArgumentException", "context and runtime generation must be valid");
    return;
  }
  jclass context_class = (*env)->GetObjectClass(env, context);
  if ((*env)->ExceptionCheck(env) || context_class == NULL) return;
  jmethodID get_loader = (*env)->GetMethodID(env, context_class, "getClassLoader", "()Ljava/lang/ClassLoader;");
  (*env)->DeleteLocalRef(env, context_class);
  if ((*env)->ExceptionCheck(env) || get_loader == NULL) return;
  jobject loader = (*env)->CallObjectMethod(env, context, get_loader);
  if ((*env)->ExceptionCheck(env)) return;
#if defined(AJNI_USE_MOONBIT_EXPORTS)
  ajni_initialize_moonbit();
#endif
  int status = ajni_rt_host_open(env, loader);
  if (loader != NULL) (*env)->DeleteLocalRef(env, loader);
  if (status != AJNI_RT_OK) {
    ajni_rt_host_throw(env);
    return;
  }
  pthread_mutex_lock(&g_lock);
  g_runtime_generation = generation;
  g_was_initialized = true;
  g_initialized = true;
  pthread_mutex_unlock(&g_lock);
}

static void ajni_native_shutdown(JNIEnv *env, jclass unused) {
  if ((*env)->ExceptionCheck(env)) return;
  if (gettid() != getpid()) {
    ajni_throw_new(env, "java/lang/IllegalStateException", "shutdown must run on Android's main thread");
    return;
  }
  (void)unused;
  pthread_mutex_lock(&g_lock);
  ajni_release_window_locked();
  g_initialized = false;
  g_runtime_generation = 0;
  pthread_mutex_unlock(&g_lock);
}

static void ajni_native_lifecycle(JNIEnv *env, jclass unused, jint state) {
  if (!ajni_android_enter(env)) return;
  (void)unused;
  ajni_emit(state, 0, 0);
  ajni_android_leave(env);
}

static void ajni_native_surface_created(JNIEnv *env, jclass unused, jobject surface, jint width, jint height) {
  (void)unused;
  if (surface == NULL) return;
  if (!ajni_android_enter(env)) return;
  ANativeWindow *window = ANativeWindow_fromSurface(env, surface);
  if ((*env)->ExceptionCheck(env) || window == NULL) {
    if (window != NULL) ANativeWindow_release(window);
    ajni_throw_new(env, "java/lang/IllegalStateException", "ANativeWindow_fromSurface failed");
    ajni_android_leave(env);
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
  ajni_android_leave(env);
}

static void ajni_native_surface_changed(JNIEnv *env, jclass unused, jint width, jint height) {
  if (!ajni_android_enter(env)) return;
  (void)unused;
  pthread_mutex_lock(&g_lock);
  ajni_draw_window_locked(0xFF7B8900);
  pthread_mutex_unlock(&g_lock);
  ajni_emit(AJNI_SURFACE_CHANGED, width, height);
  ajni_android_leave(env);
}

static void ajni_native_surface_destroyed(JNIEnv *env, jclass unused) {
  if (!ajni_android_enter(env)) return;
  (void)unused;
  pthread_mutex_lock(&g_lock);
  ajni_release_window_locked();
  pthread_mutex_unlock(&g_lock);
  ajni_emit(AJNI_SURFACE_DESTROYED, 0, 0);
  ajni_android_leave(env);
}

static void ajni_native_on_ui_task(JNIEnv *env, jclass unused, jint event_kind) {
  if (!ajni_android_enter(env)) return;
  (void)unused;
  pthread_mutex_lock(&g_lock);
  bool initialized = g_initialized;
  pthread_mutex_unlock(&g_lock);
  if (!initialized) {
    ajni_android_leave(env);
    return;
  }
  AJNI_LOGI("UI callback dispatched on Android main thread");
  if (event_kind == AJNI_WORKER_ATTACHED) ajni_emit(AJNI_WORKER_ATTACHED, 0, 0);
  ajni_emit(AJNI_UI_TASK, 0, 0);
  ajni_android_leave(env);
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
  if (!g_initialized || g_bridge_class == NULL) {
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
  AjniEnvLease lease;
  if (ajni_rt_env_acquire(&lease) != AJNI_RT_OK) {
    ajni_rt_host_throw(env);
    return;
  }
  ajni_rt_env_release(&lease);
  ajni_reset_error();
  pthread_mutex_lock(&g_lock);
  int32_t status = ajni_start_worker_locked();
  pthread_mutex_unlock(&g_lock);
  if (status != AJNI_OK) ajni_throw_new(env, "java/lang/IllegalStateException", g_error_message);
}

MOONBIT_FFI_EXPORT int32_t ajni_runtime_ready(void) {
  pthread_mutex_lock(&g_lock);
  bool initialized = g_initialized;
  pthread_mutex_unlock(&g_lock);
  if (!initialized) return 0;
  AjniEnvLease lease;
  if (ajni_rt_env_acquire(&lease) != AJNI_RT_OK) return 0;
  int status = ajni_rt_host_enter(lease.env);
  if (status == AJNI_RT_OK) ajni_rt_host_leave();
  ajni_rt_env_release(&lease);
  return status == AJNI_RT_OK;
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
  JNIEnv *env = NULL;
  if ((*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) != JNI_OK) return JNI_ERR;
  if (ajni_rt_register_companion(env) != AJNI_RT_OK) return JNI_ERR;
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
      {"nativeCreateIntCallback", "()Ldev/nanaloveyuki/ajni/AjniCallback;", (void *)ajni_native_create_int_callback},
      {"nativeHandledObserverError", "()Z", (void *)ajni_native_handled_observer_error},
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
  return JNI_VERSION_1_6;
}

JNIEXPORT void JNI_OnUnload(JavaVM *vm, void *unused) {
  (void)unused;
  JNIEnv *env = NULL;
  if ((*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) != JNI_OK) return;
  pthread_mutex_lock(&g_lock);
  g_initialized = false;
  g_runtime_generation = 0;
  ajni_release_window_locked();
  while (g_active_workers != 0 || g_active_bridge_calls != 0) pthread_cond_wait(&g_idle, &g_lock);
  jclass bridge_class = g_bridge_class;
  g_bridge_class = NULL;
  pthread_mutex_unlock(&g_lock);
  if (bridge_class != NULL) (*env)->DeleteGlobalRef(env, bridge_class);
  /* Process subscriptions belong to the owner; unload never releases managed values. */
}

#else

MOONBIT_FFI_EXPORT int32_t ajni_runtime_ready(void) { return 0; }
MOONBIT_FFI_EXPORT int32_t ajni_post_ui_callback(void) {
  return ajni_set_error(AJNI_UNSUPPORTED, "Android UI callbacks are unsupported on this platform");
}
MOONBIT_FFI_EXPORT int32_t ajni_start_worker(void) {
  return ajni_set_error(AJNI_UNSUPPORTED, "Android workers are unsupported on this platform");
}
MOONBIT_FFI_EXPORT void ajni_android_export_error(uint64_t env, uint64_t throwable) {
  (void)env;
  (void)throwable;
  ajni_set_error(AJNI_UNSUPPORTED, "Android export errors are unsupported on this platform");
}

#endif
