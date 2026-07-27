package dev.nanaloveyuki.ajni.host

import android.annotation.SuppressLint
import android.graphics.Color
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.view.ViewGroup
import android.webkit.WebChromeClient
import android.webkit.WebResourceError
import android.webkit.WebResourceRequest
import android.webkit.WebSettings
import android.webkit.WebView
import android.webkit.WebViewClient
import android.widget.FrameLayout
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.atomic.AtomicBoolean

object AjniWebViewHost {
  const val CREATE = 1
  const val NAVIGATE = 2
  const val LOAD_HTML = 3
  const val EVAL = 4
  const val DESTROY = 5

  const val CREATED = 30
  const val NAVIGATION_STARTED = 31
  const val NAVIGATION_FINISHED = 32
  const val TITLE_CHANGED = 33
  const val SCRIPT_RESULT = 34
  const val FAILED = 35
  const val DESTROYED = 36

  private val mainHandler = Handler(Looper.getMainLooper())
  private val attached = AtomicBoolean(false)
  private val webViews = ConcurrentHashMap<Long, WebView>()
  private var container: FrameLayout? = null

  fun attach(host: FrameLayout) {
    check(Looper.myLooper() == Looper.getMainLooper()) { "WebView host must attach on the main thread" }
    destroyAll()
    container = host
    attached.set(true)
  }

  fun detach(host: FrameLayout) {
    if (Looper.myLooper() == Looper.getMainLooper()) {
      if (container !== host) return
      attached.set(false)
      destroyAll()
      container = null
    } else {
      mainHandler.post { detach(host) }
    }
  }

  fun command(command: Int, handle: Long, payload: String, requestId: String): Boolean {
    if (!attached.get()) return false
    return runOnMain {
      val host = container
      if (host == null) {
        emit(FAILED, handle, "WebView host is detached")
        return@runOnMain
      }
      try {
        when (command) {
          CREATE -> create(host, handle, payload)
          NAVIGATE -> requireView(handle).loadUrl(payload)
          LOAD_HTML -> requireView(handle).loadDataWithBaseURL(null, payload, "text/html", "utf-8", null)
          EVAL -> requireView(handle).evaluateJavascript(payload) {
            emit(SCRIPT_RESULT, handle, it ?: "null", requestId)
          }
          DESTROY -> destroy(handle)
          else -> emit(FAILED, handle, "Unsupported WebView command: $command")
        }
      } catch (error: Exception) {
        Log.e("ajni", "WebView command $command failed for handle $handle", error)
        emit(FAILED, handle, error.message ?: error.javaClass.simpleName)
      }
    }
  }

  fun setBounds(handle: Long, x: Int, y: Int, width: Int, height: Int): Boolean {
    if (!attached.get() || width < 1 || height < 1) return false
    return runOnMain {
      try {
        val view = requireView(handle)
        view.layoutParams = FrameLayout.LayoutParams(width, height).apply {
          leftMargin = x
          topMargin = y
        }
      } catch (error: Exception) {
        Log.e("ajni", "WebView bounds update failed for handle $handle", error)
        emit(FAILED, handle, error.message ?: error.javaClass.simpleName)
      }
    }
  }

  @SuppressLint("SetJavaScriptEnabled")
  private fun create(host: FrameLayout, handle: Long, initialUrl: String) {
    if (webViews.containsKey(handle)) {
      emit(FAILED, handle, "WebView handle already exists")
      return
    }
    val view = WebView(host.context).apply {
      setBackgroundColor(Color.WHITE)
      settings.javaScriptEnabled = true
      settings.domStorageEnabled = true
      settings.allowFileAccess = false
      settings.allowContentAccess = false
      settings.mixedContentMode = WebSettings.MIXED_CONTENT_NEVER_ALLOW
      settings.setSupportMultipleWindows(false)
      if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) settings.safeBrowsingEnabled = true
      webViewClient = object : WebViewClient() {
        override fun onPageStarted(view: WebView, url: String?, favicon: android.graphics.Bitmap?) {
          emit(NAVIGATION_STARTED, handle, url.orEmpty())
        }

        override fun onPageFinished(view: WebView, url: String?) {
          emit(NAVIGATION_FINISHED, handle, url.orEmpty())
        }

        override fun onReceivedError(view: WebView, request: WebResourceRequest, error: WebResourceError) {
          if (request.isForMainFrame) emit(FAILED, handle, error.description?.toString().orEmpty())
        }
      }
      webChromeClient = object : WebChromeClient() {
        override fun onReceivedTitle(view: WebView, title: String?) {
          emit(TITLE_CHANGED, handle, title.orEmpty())
        }
      }
    }
    webViews[handle] = view
    host.addView(view, FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT))
    emit(CREATED, handle, initialUrl)
    if (initialUrl.isNotEmpty()) view.loadUrl(initialUrl)
  }

  private fun requireView(handle: Long): WebView = webViews[handle] ?: error("WebView handle does not exist")

  private fun destroy(handle: Long) {
    val view = webViews.remove(handle) ?: return emit(DESTROYED, handle, "")
    (view.parent as? ViewGroup)?.removeView(view)
    view.destroy()
    emit(DESTROYED, handle, "")
  }

  private fun destroyAll() {
    webViews.keys.toList().forEach(::destroy)
  }

  private fun runOnMain(action: () -> Unit): Boolean =
    if (Looper.myLooper() == Looper.getMainLooper()) {
      action()
      true
    } else {
      mainHandler.post(action)
    }

  private fun emit(kind: Int, handle: Long, payload: String, detail: String = "") {
    NativeBridge.webViewEvent(kind, handle, payload, detail)
  }
}
