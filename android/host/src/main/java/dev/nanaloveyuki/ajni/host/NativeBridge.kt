package dev.nanaloveyuki.ajni.host

import android.content.Context
import android.os.Handler
import android.os.Looper
import android.view.Surface
import android.widget.FrameLayout

/** Stable JNI entry class for applications that link ajni's native library. */
object NativeBridge {
  const val LIFECYCLE_CREATED = 1
  const val LIFECYCLE_STARTED = 2
  const val LIFECYCLE_RESUMED = 3
  const val LIFECYCLE_PAUSED = 4
  const val LIFECYCLE_STOPPED = 5
  const val LIFECYCLE_DESTROYED = 6
  private const val UI_TASK_EVENT = 21
  @Volatile private var runtimeGeneration = 0L
  private var nextRuntimeGeneration = 1L
  private var runtimeTransition = false
  private val mainHandler = Handler(Looper.getMainLooper())

  init {
    System.loadLibrary("ajni")
  }

  fun initialize(context: Context) {
    requireMainThread("initialize")
    if (runtimeGeneration != 0L) {
      nativeInitialize(context.applicationContext, runtimeGeneration)
      return
    }
    check(!runtimeTransition) { "NativeBridge lifecycle transition is in progress" }
    val generation = nextRuntimeGeneration++
    check(generation > 0L) { "NativeBridge runtime generation exhausted" }
    runtimeTransition = true
    try {
      nativeInitialize(context.applicationContext, generation)
      runtimeGeneration = generation
    } finally {
      runtimeTransition = false
    }
  }

  fun shutdown() {
    requireMainThread("shutdown")
    if (runtimeGeneration == 0L) return
    runtimeTransition = true
    runtimeGeneration = 0L
    try {
      nativeShutdown()
    } finally {
      try {
        AjniWebViewHost.runtimeShutdown()
      } finally {
        runtimeTransition = false
      }
    }
  }

  internal fun currentRuntimeGeneration(): Long = runtimeGeneration

  internal fun matchesRuntimeGeneration(generation: Long): Boolean =
    generation != 0L && runtimeGeneration == generation

  fun lifecycle(state: Int) {
    requireMainThread("lifecycle")
    val generation = currentRuntimeGeneration()
    if (matchesRuntimeGeneration(generation)) {
      nativeLifecycle(state)
      if (!matchesRuntimeGeneration(generation)) return
      when (state) {
        LIFECYCLE_RESUMED -> AjniWebViewHost.resume()
        LIFECYCLE_PAUSED -> AjniWebViewHost.pause()
      }
    }
  }

  fun surfaceCreated(surface: Surface, width: Int, height: Int) {
    requireMainThread("surfaceCreated")
    if (runtimeGeneration != 0L) nativeSurfaceCreated(surface, width, height)
  }

  fun surfaceChanged(width: Int, height: Int) {
    requireMainThread("surfaceChanged")
    if (runtimeGeneration != 0L) nativeSurfaceChanged(width, height)
  }

  fun surfaceDestroyed() {
    requireMainThread("surfaceDestroyed")
    if (runtimeGeneration != 0L) nativeSurfaceDestroyed()
  }

  fun startWorker() {
    check(runtimeGeneration != 0L) { "NativeBridge is not initialized" }
    nativeStartWorker()
  }

  fun echo(value: String): String = nativeEcho(value)

  fun createIntCallbackForTesting(): dev.nanaloveyuki.ajni.AjniCallback =
    nativeCreateIntCallback()

  fun handledObserverErrorForTesting(): Boolean = nativeHandledObserverError()

  fun attachWebViewContainer(container: FrameLayout) {
    AjniWebViewHost.attach(container)
  }

  fun detachWebViewContainer(container: FrameLayout) {
    AjniWebViewHost.detach(container)
  }

  @JvmStatic
  fun postUiCallback() {
    postUiCallback(UI_TASK_EVENT)
  }

  @JvmStatic
  fun postUiCallback(eventKind: Int): Boolean =
    postUiCallbackForRuntime(eventKind, currentRuntimeGeneration())

  @JvmStatic
  private fun postUiCallbackForRuntime(eventKind: Int, runtimeGeneration: Long): Boolean {
    if (!matchesRuntimeGeneration(runtimeGeneration)) return false
    return mainHandler.post {
      if (matchesRuntimeGeneration(runtimeGeneration)) nativeOnUiTask(eventKind)
    }
  }

  @JvmStatic
  fun webViewCommand(command: Int, handle: Long, payload: String, requestId: String): Boolean =
    AjniWebViewHost.command(command, handle, payload, requestId)

  @JvmStatic
  private fun webViewCommandForRuntime(
    command: Int, handle: Long, payload: String, requestId: String, runtimeGeneration: Long,
  ): Boolean = matchesRuntimeGeneration(runtimeGeneration) &&
    AjniWebViewHost.commandForRuntime(command, handle, payload, requestId, runtimeGeneration)

  @JvmStatic
  fun webViewSetBounds(handle: Long, x: Int, y: Int, width: Int, height: Int): Boolean =
    AjniWebViewHost.setBounds(handle, x, y, width, height)

  @JvmStatic
  private fun webViewSetBoundsForRuntime(
    handle: Long, x: Int, y: Int, width: Int, height: Int, runtimeGeneration: Long,
  ): Boolean = matchesRuntimeGeneration(runtimeGeneration) &&
    AjniWebViewHost.setBoundsForRuntime(handle, x, y, width, height, runtimeGeneration)

  @JvmStatic
  fun webViewEvent(kind: Int, handle: Long, payload: String, detail: String = "") {
    requireMainThread("webViewEvent")
    if (runtimeGeneration != 0L) nativeWebViewEvent(kind, handle, payload, detail)
  }

  internal fun webViewEventForRuntime(
    kind: Int, handle: Long, payload: String, detail: String, generation: Long,
  ) {
    requireMainThread("webViewEvent")
    if (matchesRuntimeGeneration(generation)) nativeWebViewEvent(kind, handle, payload, detail)
  }

  /** Posts a WebView event from a WebKit worker thread without entering JNI there. */
  @JvmStatic
  fun postWebViewEvent(kind: Int, handle: Long, payload: String, detail: String = ""): Boolean {
    val generation = currentRuntimeGeneration()
    if (!matchesRuntimeGeneration(generation)) return false
    val isCurrentView = AjniWebViewHost.eventIdentityCheck(handle)
    return mainHandler.post {
      if (matchesRuntimeGeneration(generation) && isCurrentView()) nativeWebViewEvent(kind, handle, payload, detail)
    }
  }

  private fun requireMainThread(operation: String) {
    check(Looper.myLooper() == Looper.getMainLooper()) { "NativeBridge.$operation must run on the main thread" }
  }

  @JvmStatic private external fun nativeInitialize(context: Context, runtimeGeneration: Long)
  @JvmStatic private external fun nativeShutdown()
  @JvmStatic private external fun nativeLifecycle(state: Int)
  @JvmStatic private external fun nativeSurfaceCreated(surface: Surface, width: Int, height: Int)
  @JvmStatic private external fun nativeSurfaceChanged(width: Int, height: Int)
  @JvmStatic private external fun nativeSurfaceDestroyed()
  @JvmStatic private external fun nativeOnUiTask(eventKind: Int)
  @JvmStatic private external fun nativeStartWorker()
  @JvmStatic private external fun nativeEcho(value: String): String
  @JvmStatic private external fun nativeCreateIntCallback(): dev.nanaloveyuki.ajni.AjniCallback
  @JvmStatic private external fun nativeHandledObserverError(): Boolean
  @JvmStatic private external fun nativeWebViewEvent(kind: Int, handle: Long, payload: String, detail: String)
}
