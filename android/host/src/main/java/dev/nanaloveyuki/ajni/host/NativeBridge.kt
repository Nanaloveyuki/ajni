package dev.nanaloveyuki.ajni.host

import android.content.Context
import android.os.Handler
import android.os.Looper
import android.view.Surface
import android.widget.FrameLayout
import java.util.concurrent.atomic.AtomicBoolean

/** Stable JNI entry class for applications that link ajni's native library. */
object NativeBridge {
  private val initialized = AtomicBoolean(false)
  private val mainHandler = Handler(Looper.getMainLooper())

  init {
    System.loadLibrary("ajni")
  }

  fun initialize(context: Context) {
    if (initialized.compareAndSet(false, true)) {
      nativeInitialize(context.applicationContext)
    }
  }

  fun shutdown() {
    if (initialized.compareAndSet(true, false)) {
      nativeShutdown()
    }
  }

  fun lifecycle(state: Int) {
    if (initialized.get()) nativeLifecycle(state)
  }

  fun surfaceCreated(surface: Surface, width: Int, height: Int) {
    if (initialized.get()) nativeSurfaceCreated(surface, width, height)
  }

  fun surfaceChanged(width: Int, height: Int) {
    if (initialized.get()) nativeSurfaceChanged(width, height)
  }

  fun surfaceDestroyed() {
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
    mainHandler.post { nativeOnUiTask() }
  }

  @JvmStatic
  fun webViewCommand(command: Int, handle: Long, payload: String, requestId: String): Boolean =
    AjniWebViewHost.command(command, handle, payload, requestId)

  @JvmStatic
  fun webViewSetBounds(handle: Long, x: Int, y: Int, width: Int, height: Int): Boolean =
    AjniWebViewHost.setBounds(handle, x, y, width, height)

  @JvmStatic
  fun webViewEvent(kind: Int, handle: Long, payload: String, detail: String = "") {
    if (initialized.get()) nativeWebViewEvent(kind, handle, payload, detail)
  }

  @JvmStatic private external fun nativeInitialize(context: Context)
  @JvmStatic private external fun nativeShutdown()
  @JvmStatic private external fun nativeLifecycle(state: Int)
  @JvmStatic private external fun nativeSurfaceCreated(surface: Surface, width: Int, height: Int)
  @JvmStatic private external fun nativeSurfaceChanged(width: Int, height: Int)
  @JvmStatic private external fun nativeSurfaceDestroyed()
  @JvmStatic private external fun nativeOnUiTask()
  @JvmStatic private external fun nativeStartWorker()
  @JvmStatic private external fun nativeEcho(value: String): String
  @JvmStatic private external fun nativeWebViewEvent(kind: Int, handle: Long, payload: String, detail: String)
}
