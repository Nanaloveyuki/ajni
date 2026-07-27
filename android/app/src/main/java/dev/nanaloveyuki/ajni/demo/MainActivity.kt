package dev.nanaloveyuki.ajni.demo

import android.graphics.PixelFormat
import android.os.Bundle
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.ViewGroup
import android.widget.Button
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.TextView
import androidx.activity.ComponentActivity
import androidx.lifecycle.DefaultLifecycleObserver
import androidx.lifecycle.LifecycleOwner

class MainActivity : ComponentActivity(), SurfaceHolder.Callback {
  private lateinit var status: TextView
  private lateinit var surface: SurfaceView
  private lateinit var webViewContainer: FrameLayout

  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)
    NativeBridge.initialize(this)
    status = TextView(this).apply {
      setPadding(24, 24, 24, 24)
      text = "JNI ready; waiting for Surface"
    }
    surface = SurfaceView(this).apply {
      holder.setFormat(PixelFormat.RGBA_8888)
      holder.addCallback(this@MainActivity)
    }
    val worker = Button(this).apply {
      text = "Start native worker"
      setOnClickListener {
        NativeBridge.startWorker()
        status.text = "Native worker attached and UI callback posted"
      }
    }
    webViewContainer = FrameLayout(this).apply {
      addView(surface, FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT))
    }
    val root = LinearLayout(this).apply {
      orientation = LinearLayout.VERTICAL
      addView(status, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))
      addView(webViewContainer, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f))
      addView(worker, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))
    }
    setContentView(root)
    NativeBridge.attachWebViewContainer(webViewContainer)
    lifecycle.addObserver(object : DefaultLifecycleObserver {
      override fun onCreate(owner: LifecycleOwner) = forward(1, "created")
      override fun onStart(owner: LifecycleOwner) = forward(2, "started")
      override fun onResume(owner: LifecycleOwner) = forward(3, "resumed")
      override fun onPause(owner: LifecycleOwner) = forward(4, "paused")
      override fun onStop(owner: LifecycleOwner) = forward(5, "stopped")
      override fun onDestroy(owner: LifecycleOwner) = forward(6, "destroyed")
    })
  }

  override fun surfaceCreated(holder: SurfaceHolder) {
    val frame = holder.surfaceFrame
    NativeBridge.surfaceCreated(holder.surface, frame.width(), frame.height())
    status.text = "Surface created: ${frame.width()} x ${frame.height()}"
  }

  override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
    NativeBridge.surfaceChanged(width, height)
    status.text = "Surface changed: $width x $height"
  }

  override fun surfaceDestroyed(holder: SurfaceHolder) {
    NativeBridge.surfaceDestroyed()
    status.text = "Surface destroyed"
  }

  private fun forward(state: Int, label: String) {
    NativeBridge.lifecycle(state)
    if (::status.isInitialized) status.text = "Lifecycle: $label"
  }

  override fun onDestroy() {
    NativeBridge.detachWebViewContainer(webViewContainer)
    NativeBridge.shutdown()
    super.onDestroy()
  }
}
