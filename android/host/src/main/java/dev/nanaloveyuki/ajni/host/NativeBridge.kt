package dev.nanaloveyuki.ajni.host

import android.content.Context
import android.os.Handler
import android.os.Looper
import android.view.Surface
import android.widget.FrameLayout
import java.util.concurrent.atomic.AtomicBoolean

/** Stable JNI entry class for applications that link ajni's native library. */
object NativeBridge {
  private const val UI_TASK_EVENT = 21
  private val initialized = AtomicBoolean(false)
  private val mainHandler = Handler(Looper.getMainLooper())

  init {
    System.loadLibrary("ajni")
  }

  fun initialize(context: Context) {
    if (initialized.compareAndSet(false, true)) {
      try {
        nativeInitialize(context.applicationContext)
      } catch (error: Throwable) {
        initialized.set(false)
        throw error
      }
    }
  }

  fun shutdown() {
    if (initialized.compareAndSet(true, false)) {
      nativeShutdown()
    }
  }

  fun lifecycle(state: Int) {
    requireMainThread("lifecycle")
    if (initialized.get()) nativeLifecycle(state)
  }

  fun surfaceCreated(surface: Surface, width: Int, height: Int) {
    requireMainThread("surfaceCreated")
    if (initialized.get()) nativeSurfaceCreated(surface, width, height)
  }

  fun surfaceChanged(width: Int, height: Int) {
    requireMainThread("surfaceChanged")
    if (initialized.get()) nativeSurfaceChanged(width, height)
  }

  fun surfaceDestroyed() {
    requireMainThread("surfaceDestroyed")
    if (initialized.get()) nativeSurfaceDestroyed()
  }

  fun startWorker() {
    check(initialized.get()) { "NativeBridge is not initialized" }
    nativeStartWorker()
  }

  fun echo(value: String): String = nativeEcho(value)

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
  fun postUiCallback(eventKind: Int): Boolean {
    if (!initialized.get()) return false
    return mainHandler.post {
      if (initialized.get()) nativeOnUiTask(eventKind)
    }
  }

  @JvmStatic
  fun webViewCommand(command: Int, handle: Long, payload: String, requestId: String): Boolean =
    AjniWebViewHost.command(command, handle, payload, requestId)

  @JvmStatic
  fun webViewSetBounds(handle: Long, x: Int, y: Int, width: Int, height: Int): Boolean =
    AjniWebViewHost.setBounds(handle, x, y, width, height)

  @JvmStatic
  fun webViewEvent(kind: Int, handle: Long, payload: String, detail: String = "") {
    requireMainThread("webViewEvent")
    if (initialized.get()) nativeWebViewEvent(kind, handle, payload, detail)
  }

  private fun requireMainThread(operation: String) {
    check(Looper.myLooper() == Looper.getMainLooper()) { "NativeBridge.$operation must run on the main thread" }
  }

  @JvmStatic private external fun nativeInitialize(context: Context)
  @JvmStatic private external fun nativeShutdown()
  @JvmStatic private external fun nativeLifecycle(state: Int)
  @JvmStatic private external fun nativeSurfaceCreated(surface: Surface, width: Int, height: Int)
  @JvmStatic private external fun nativeSurfaceChanged(width: Int, height: Int)
  @JvmStatic private external fun nativeSurfaceDestroyed()
  @JvmStatic private external fun nativeOnUiTask(eventKind: Int)
  @JvmStatic private external fun nativeStartWorker()
  @JvmStatic private external fun nativeEcho(value: String): String
  @JvmStatic private external fun nativeWebViewEvent(kind: Int, handle: Long, payload: String, detail: String)
}
