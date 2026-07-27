package dev.nanaloveyuki.ajni.demo

import android.content.Context
import android.os.Handler
import android.os.Looper
import android.view.Surface
import java.util.concurrent.atomic.AtomicBoolean

internal object NativeBridge {
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

  @JvmStatic
  fun postUiCallback() {
    mainHandler.post { nativeOnUiTask() }
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
}
