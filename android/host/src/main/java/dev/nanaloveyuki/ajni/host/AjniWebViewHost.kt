package dev.nanaloveyuki.ajni.host

import android.annotation.SuppressLint
import android.graphics.Color
import android.net.Uri
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.util.Base64
import android.util.Log
import android.view.ViewGroup
import android.webkit.WebChromeClient
import android.webkit.WebResourceError
import android.webkit.WebResourceRequest
import android.webkit.WebResourceResponse
import android.webkit.WebSettings
import android.webkit.WebView
import android.webkit.WebViewClient
import android.widget.FrameLayout
import androidx.webkit.ScriptHandler
import androidx.webkit.WebMessageCompat
import androidx.webkit.WebViewAssetLoader
import androidx.webkit.WebViewCompat
import androidx.webkit.WebViewFeature
import androidx.annotation.VisibleForTesting
import java.io.ByteArrayInputStream
import java.nio.charset.StandardCharsets
import java.util.concurrent.CompletableFuture
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicLong

/** Main-Looper WebView host. Structured command/event payload values are Base64url UTF-8. */
object AjniWebViewHost {
  const val CREATE = 1
  const val NAVIGATE = 2
  const val LOAD_HTML = 3
  const val EVAL = 4
  const val DESTROY = 5
  const val CREATE_WITH_OPTIONS = 6
  const val LOAD_HTML_WITH_BASE_URL = 7
  const val POST_MESSAGE = 8
  const val RESPOND_ASSET = 9
  const val ADD_DOCUMENT_START_SCRIPT = 10

  const val CREATED = 30
  const val NAVIGATION_STARTED = 31
  const val NAVIGATION_FINISHED = 32
  const val TITLE_CHANGED = 33
  const val SCRIPT_RESULT = 34
  const val FAILED = 35
  const val DESTROYED = 36
  const val PAGE_MESSAGE = 37
  const val ASSET_REQUEST = 38
  const val OPERATION_FAILED = 39

  private const val TAG = "ajni"
  private const val MESSAGE_LISTENER_NAME = "ajni"
  private const val ASSET_PREFIX = "/assets/"
  private const val DEFAULT_PAGE_MESSAGE_BYTES = 1024 * 1024L
  private const val DEFAULT_PENDING_ASSET_REQUESTS = 32
  private const val DEFAULT_ASSET_RESPONSE_BYTES = 8L * 1024 * 1024
  private const val DEFAULT_ASSET_TIMEOUT_MILLIS = 10_000L

  private val mainHandler = Handler(Looper.getMainLooper())
  private val attached = AtomicBoolean(false)
  private val webViews = ConcurrentHashMap<Long, ViewState>()
  private val pendingAssets = ConcurrentHashMap<String, PendingAsset>()
  private val nextAssetId = AtomicLong(1)
  private var container: FrameLayout? = null
  @Volatile private var eventObserverForTesting: ((Int, Long, String, String) -> Unit)? = null

  private data class ResourceLimits(
    val maxPageMessageBytes: Long,
    val maxPendingAssetRequests: Int,
    val maxAssetResponseBytes: Long,
    val assetResponseTimeoutMillis: Long,
  )

  private data class CreateOptions(
    val origin: Uri,
    val initialKind: String,
    val initial: String,
    val baseUrl: String,
    val scripts: List<String>,
    val limits: ResourceLimits,
  )

  private class ViewState(
    val handle: Long,
    val view: WebView,
    val origin: Uri?,
    val assetLoader: WebViewAssetLoader?,
    val limits: ResourceLimits,
  ) {
    val scripts = mutableListOf<ScriptHandler>()
    var currentNavigationOperation = ""
    var paused = false
    var destroyed = false
  }

  private data class AssetResponse(
    val status: Int,
    val mime: String,
    val encoding: String?,
    val headers: Map<String, String>,
    val body: ByteArray,
  )

  private data class PendingAsset(val handle: Long, val future: CompletableFuture<AssetResponse>)

  fun attach(host: FrameLayout) {
    requireMainThread("attach")
    destroyAll()
    container = host
    attached.set(true)
  }

  fun detach(host: FrameLayout) {
    if (Looper.myLooper() != Looper.getMainLooper()) {
      mainHandler.post { detach(host) }
      return
    }
    if (container !== host) return
    attached.set(false)
    destroyAll()
    completePendingAssets(null, unavailableAssetResponse(503, "WebView host detached"))
    container = null
  }

  fun pause() {
    requireMainThread("pause")
    webViews.values.forEach { state ->
      if (isLive(state) && !state.paused) {
        state.view.onPause()
        state.paused = true
      }
    }
  }

  fun resume() {
    requireMainThread("resume")
    webViews.values.forEach { state ->
      if (isLive(state) && state.paused) {
        state.view.onResume()
        state.paused = false
      }
    }
  }

  fun command(command: Int, handle: Long, payload: String, requestId: String): Boolean {
    if (!attached.get()) return false
    return runOnMain {
      if (!attached.get() || container == null) {
        operationFailed(handle, requestId, "WebView host is detached")
        return@runOnMain
      }
      try {
        when (command) {
          CREATE -> createLegacy(handle, payload, requestId)
          CREATE_WITH_OPTIONS -> createWithOptions(handle, parseFields(payload), requestId)
          NAVIGATE -> navigate(handle, payload, requestId)
          LOAD_HTML -> loadHtml(handle, payload, null, requestId)
          LOAD_HTML_WITH_BASE_URL -> {
            val fields = parseFields(payload)
            loadHtml(handle, fields.required("html"), fields.required("base_url"), requestId)
          }
          EVAL -> requireView(handle).view.evaluateJavascript(payload) {
            emit(SCRIPT_RESULT, handle, it ?: "null", requestId)
          }
          POST_MESSAGE -> postMessage(handle, parseFields(payload).required("body"))
          RESPOND_ASSET -> respondAsset(handle, parseFields(payload), requestId)
          ADD_DOCUMENT_START_SCRIPT -> addDocumentStartScript(handle, parseFields(payload).required("script"))
          DESTROY -> destroy(handle, requestId)
          else -> operationFailed(handle, requestId, "Unsupported WebView command: $command")
        }
      } catch (error: Exception) {
        Log.e(TAG, "WebView command $command failed for handle $handle", error)
        operationFailed(handle, requestId, error.message ?: error.javaClass.simpleName)
      }
    }
  }

  fun setBounds(handle: Long, x: Int, y: Int, width: Int, height: Int): Boolean {
    if (!attached.get() || width < 1 || height < 1) return false
    return runOnMain {
      try {
        requireView(handle).view.layoutParams = FrameLayout.LayoutParams(width, height).apply {
          leftMargin = x
          topMargin = y
        }
      } catch (error: Exception) {
        Log.e(TAG, "WebView bounds update failed for handle $handle", error)
        emit(FAILED, handle, error.message ?: error.javaClass.simpleName)
      }
    }
  }

  @VisibleForTesting
  fun setEventObserverForTesting(observer: ((Int, Long, String, String) -> Unit)?) {
    eventObserverForTesting = observer
  }

  @VisibleForTesting
  fun isWebViewPausedForTesting(handle: Long): Boolean = webViews[handle]?.paused ?: false

  private fun createLegacy(handle: Long, initialUrl: String, operationId: String) {
    if (webViews.containsKey(handle)) {
      operationFailed(handle, operationId, "WebView handle already exists")
      return
    }
    val view = newWebView(requireNotNull(container))
    val state = ViewState(handle, view, null, null, defaultLimits())
    configureClients(state)
    webViews[handle] = state
    requireNotNull(container).addView(view, fullSize())
    emit(CREATED, handle, initialUrl, operationId)
    if (initialUrl.isNotEmpty()) navigate(handle, initialUrl, operationId)
  }

  private fun createWithOptions(handle: Long, fields: Map<String, String>, operationId: String) {
    if (webViews.containsKey(handle)) {
      operationFailed(handle, operationId, "WebView handle already exists")
      return
    }
    val options = parseCreateOptions(fields)
    requireWebKitFeatures()
    val view = newWebView(requireNotNull(container))
    val assetLoader = WebViewAssetLoader.Builder()
      .setDomain(requireNotNull(options.origin.host))
      .addPathHandler(ASSET_PREFIX, DynamicAssetPathHandler(handle))
      .build()
    val state = ViewState(handle, view, options.origin, assetLoader, options.limits)
    configureSecureChannel(state)
    configureClients(state)
    try {
      options.scripts.forEach { addScript(state, it) }
      webViews[handle] = state
      requireNotNull(container).addView(view, fullSize())
      emit(CREATED, handle, options.initial, operationId)
      when (options.initialKind) {
        "empty" -> Unit
        "url" -> navigate(handle, options.initial, operationId)
        "html" -> loadHtml(handle, options.initial, options.baseUrl, operationId)
      }
    } catch (error: Exception) {
      state.scripts.forEach(ScriptHandler::remove)
      view.destroy()
      throw error
    }
  }

  @SuppressLint("SetJavaScriptEnabled")
  private fun newWebView(host: FrameLayout): WebView = WebView(host.context).apply {
    setBackgroundColor(Color.WHITE)
    settings.javaScriptEnabled = true
    settings.domStorageEnabled = true
    settings.allowFileAccess = false
    settings.allowContentAccess = false
    settings.mixedContentMode = WebSettings.MIXED_CONTENT_NEVER_ALLOW
    settings.setSupportMultipleWindows(false)
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) settings.safeBrowsingEnabled = true
  }

  private fun configureSecureChannel(state: ViewState) {
    val origin = requireNotNull(state.origin).toString()
    WebViewCompat.addWebMessageListener(state.view, MESSAGE_LISTENER_NAME, setOf(origin)) {
        _, message, sourceOrigin, isMainFrame, _ ->
      if (!isLive(state)) return@addWebMessageListener
      val body = message.data ?: ""
      if (state.limits.maxPageMessageBytes > 0 &&
        body.toByteArray(StandardCharsets.UTF_8).size.toLong() > state.limits.maxPageMessageBytes) {
        operationFailed(state.handle, "", "Page message exceeds configured size limit")
        return@addWebMessageListener
      }
      emit(PAGE_MESSAGE, state.handle, encodeFields(mapOf(
        "body" to body,
        "origin" to sourceOrigin.toString(),
        "main_frame" to if (isMainFrame) "1" else "0",
      )))
    }
  }

  private fun configureClients(state: ViewState) {
    state.view.webViewClient = object : WebViewClient() {
      override fun onPageStarted(view: WebView, url: String?, favicon: android.graphics.Bitmap?) {
        if (isLive(state)) emit(NAVIGATION_STARTED, state.handle, url.orEmpty(), state.currentNavigationOperation)
      }

      override fun onPageFinished(view: WebView, url: String?) {
        if (isLive(state)) emit(NAVIGATION_FINISHED, state.handle, url.orEmpty(), state.currentNavigationOperation)
      }

      override fun onReceivedError(view: WebView, request: WebResourceRequest, error: WebResourceError) {
        if (isLive(state) && request.isForMainFrame) {
          operationFailed(state.handle, state.currentNavigationOperation, error.description?.toString().orEmpty())
        }
      }

      override fun shouldInterceptRequest(view: WebView, request: WebResourceRequest): WebResourceResponse? =
        state.assetLoader?.shouldInterceptRequest(request.url)
    }
    state.view.webChromeClient = object : WebChromeClient() {
      override fun onReceivedTitle(view: WebView, title: String?) {
        if (isLive(state)) emit(TITLE_CHANGED, state.handle, title.orEmpty())
      }
    }
  }

  private fun navigate(handle: Long, url: String, operationId: String) {
    val state = requireView(handle)
    state.origin?.let { origin ->
      val destination = Uri.parse(url)
      require(
        destination.scheme == "https" &&
          destination.host == origin.host &&
          destination.port == -1 &&
          destination.userInfo == null &&
          destination.path?.startsWith(ASSET_PREFIX) == true,
      ) { "Navigation must remain within the configured asset origin" }
    }
    state.currentNavigationOperation = operationId
    state.view.loadUrl(url)
  }

  private fun loadHtml(handle: Long, html: String, baseUrl: String?, operationId: String) {
    val state = requireView(handle)
    if (baseUrl != null && state.origin != null && !baseUrl.startsWith("${state.origin}/assets/")) {
      throw IllegalArgumentException("HTML base URL must use the configured asset origin")
    }
    state.currentNavigationOperation = operationId
    state.view.loadDataWithBaseURL(baseUrl, html, "text/html", "utf-8", null)
  }

  private fun postMessage(handle: Long, body: String) {
    val state = requireView(handle)
    val origin = state.origin ?: throw IllegalStateException("WebView was not created with a trusted origin")
    if (!WebViewFeature.isFeatureSupported(WebViewFeature.POST_WEB_MESSAGE)) {
      throw IllegalStateException("WebView POST_WEB_MESSAGE feature is unavailable")
    }
    WebViewCompat.postWebMessage(state.view, WebMessageCompat(body), origin)
  }

  private fun addDocumentStartScript(handle: Long, script: String) {
    val state = requireView(handle)
    if (state.origin == null) throw IllegalStateException("WebView was not created with a trusted origin")
    addScript(state, script)
  }

  private fun addScript(state: ViewState, script: String) {
    state.scripts += WebViewCompat.addDocumentStartJavaScript(state.view, script, setOf(requireNotNull(state.origin).toString()))
  }

  private fun respondAsset(handle: Long, fields: Map<String, String>, operationId: String) {
    val requestId = fields.required("request_id")
    val pending = pendingAssets.remove(requestId)
    if (pending == null || pending.handle != handle) {
      operationFailed(handle, operationId, "Unknown asset request")
      return
    }
    val state = webViews[handle]
    if (state == null || state.destroyed) {
      pending.future.complete(unavailableAssetResponse(503, "WebView destroyed"))
      return
    }
    val body = decodeBase64Bytes(fields.required("body"))
    if (state.limits.maxAssetResponseBytes > 0 && body.size.toLong() > state.limits.maxAssetResponseBytes) {
      pending.future.complete(unavailableAssetResponse(413, "Asset response exceeds configured size limit"))
      return
    }
    val status = fields.required("status").toIntOrNull()?.takeIf { it in 100..599 }
      ?: throw IllegalArgumentException("Asset response status must be 100..599")
    pending.future.complete(AssetResponse(
      status,
      fields.required("mime"),
      fields["encoding"]?.takeIf(String::isNotEmpty),
      parseHeaders(fields["headers"].orEmpty()),
      body,
    ))
  }

  private fun destroy(handle: Long, operationId: String = "") {
    val state = webViews.remove(handle)
    if (state == null) {
      emit(DESTROYED, handle, "", operationId)
      return
    }
    state.destroyed = true
    completePendingAssets(handle, unavailableAssetResponse(503, "WebView destroyed"))
    if (state.origin != null && WebViewFeature.isFeatureSupported(WebViewFeature.WEB_MESSAGE_LISTENER)) {
      WebViewCompat.removeWebMessageListener(state.view, MESSAGE_LISTENER_NAME)
    }
    state.scripts.forEach(ScriptHandler::remove)
    (state.view.parent as? ViewGroup)?.removeView(state.view)
    state.view.destroy()
    emit(DESTROYED, handle, "", operationId)
  }

  private fun destroyAll() = webViews.keys.toList().forEach { destroy(it) }

  private class DynamicAssetPathHandler(private val handle: Long) : WebViewAssetLoader.PathHandler {
    override fun handle(path: String): WebResourceResponse {
      val state = webViews[handle] ?: return unavailableAssetResponse(503, "WebView destroyed").toWebResponse()
      val normalized = normalizeAssetPath(path) ?: return unavailableAssetResponse(400, "Invalid asset path").toWebResponse()
      val id = "${handle}:${nextAssetId.getAndIncrement()}"
      if (state.limits.maxPendingAssetRequests > 0 && pendingAssets.values.count { it.handle == handle } >= state.limits.maxPendingAssetRequests) {
        return unavailableAssetResponse(429, "Too many pending asset requests").toWebResponse()
      }
      val future = CompletableFuture<AssetResponse>()
      val pending = PendingAsset(handle, future)
      if (!admitPendingAsset(id, pending, state.limits.maxPendingAssetRequests)) {
        return unavailableAssetResponse(429, "Too many pending asset requests").toWebResponse()
      }
      if (!mainHandler.post {
          if (isLive(state) && pendingAssets.containsKey(id)) {
            emit(ASSET_REQUEST, handle, encodeFields(mapOf("request_id" to id, "path" to normalized)))
          } else if (pendingAssets.remove(id, pending)) {
            pending.future.complete(unavailableAssetResponse(503, "WebView destroyed"))
          }
        }) {
        pendingAssets.remove(id, pending)
        return unavailableAssetResponse(503, "Native runtime unavailable").toWebResponse()
      }
      return try {
        val response = if (state.limits.assetResponseTimeoutMillis == 0L) future.get()
        else future.get(state.limits.assetResponseTimeoutMillis, TimeUnit.MILLISECONDS)
        response.toWebResponse()
      } catch (_: Exception) {
        pendingAssets.remove(id, pending)
        unavailableAssetResponse(504, "Asset response timed out").toWebResponse()
      }
    }
  }

  private fun AssetResponse.toWebResponse() = WebResourceResponse(
    mime.ifEmpty { "application/octet-stream" }, encoding, status, reasonPhrase(status), headers, ByteArrayInputStream(body),
  )

  private fun unavailableAssetResponse(status: Int, message: String) = AssetResponse(
    status, "text/plain", "utf-8", mapOf("Cache-Control" to "no-store"), message.toByteArray(StandardCharsets.UTF_8),
  )

  private fun completePendingAssets(handle: Long?, response: AssetResponse) {
    pendingAssets.entries.toList().forEach { (id, pending) ->
      if (handle == null || pending.handle == handle) {
        if (pendingAssets.remove(id, pending)) pending.future.complete(response)
      }
    }
  }

  private fun admitPendingAsset(id: String, pending: PendingAsset, limit: Int): Boolean = synchronized(pendingAssets) {
    if (limit > 0 && pendingAssets.values.count { it.handle == pending.handle } >= limit) false
    else {
      pendingAssets[id] = pending
      true
    }
  }

  private fun parseCreateOptions(fields: Map<String, String>): CreateOptions {
    val origin = parseOrigin(fields.required("origin"))
    val initialKind = fields.required("initial_kind")
    require(initialKind in setOf("empty", "url", "html")) { "initial_kind must be empty, url, or html" }
    val initial = fields["initial"].orEmpty()
    val baseUrl = fields["base_url"].orEmpty()
    require((initialKind == "empty" && initial.isEmpty()) || (initialKind != "empty" && initial.isNotEmpty())) {
      "Initial content must be exclusive and match initial_kind"
    }
    require(initialKind != "html" || baseUrl.isNotEmpty()) { "HTML initial content requires base_url" }
    val scripts = fields["scripts"].orEmpty().split(',').filter(String::isNotEmpty).map(::decodeBase64)
    return CreateOptions(origin, initialKind, initial, baseUrl, scripts, ResourceLimits(
      limit(fields, "max_page_message_bytes", DEFAULT_PAGE_MESSAGE_BYTES, Long.MAX_VALUE),
      limit(fields, "max_pending_asset_requests", DEFAULT_PENDING_ASSET_REQUESTS.toLong(), Int.MAX_VALUE.toLong()).toInt(),
      limit(fields, "max_asset_response_bytes", DEFAULT_ASSET_RESPONSE_BYTES, Long.MAX_VALUE),
      limit(fields, "asset_response_timeout_millis", DEFAULT_ASSET_TIMEOUT_MILLIS, Long.MAX_VALUE),
    ))
  }

  private fun defaultLimits() = ResourceLimits(DEFAULT_PAGE_MESSAGE_BYTES, DEFAULT_PENDING_ASSET_REQUESTS, DEFAULT_ASSET_RESPONSE_BYTES, DEFAULT_ASSET_TIMEOUT_MILLIS)

  private fun parseOrigin(value: String): Uri {
    val parsed = Uri.parse(value)
    require(parsed.scheme == "https" && !parsed.host.isNullOrEmpty() && parsed.userInfo == null &&
      parsed.port == -1 && (parsed.path.isNullOrEmpty() || parsed.path == "/") && parsed.query == null && parsed.fragment == null) {
      "trusted origin must be an HTTPS origin without a path"
    }
    return Uri.Builder().scheme("https").authority(parsed.host).build()
  }

  private fun requireWebKitFeatures() {
    require(WebViewFeature.isFeatureSupported(WebViewFeature.WEB_MESSAGE_LISTENER)) { "WebView WEB_MESSAGE_LISTENER feature is unavailable" }
    require(WebViewFeature.isFeatureSupported(WebViewFeature.DOCUMENT_START_SCRIPT)) { "WebView DOCUMENT_START_SCRIPT feature is unavailable" }
    require(WebViewFeature.isFeatureSupported(WebViewFeature.POST_WEB_MESSAGE)) { "WebView POST_WEB_MESSAGE feature is unavailable" }
  }

  private fun parseFields(payload: String): Map<String, String> {
    val fields = linkedMapOf<String, String>()
    payload.split('&').filter(String::isNotEmpty).forEach { part ->
      val separator = part.indexOf('=')
      require(separator > 0) { "Invalid WebView command payload" }
      val key = part.substring(0, separator)
      require(key.matches(Regex("[a-z_]+"))) { "Invalid WebView command field" }
      require(fields.put(key, decodeBase64(part.substring(separator + 1))) == null) { "Duplicate WebView command field: $key" }
    }
    return fields
  }

  private fun Map<String, String>.required(name: String) = this[name] ?: throw IllegalArgumentException("Missing $name")

  private fun encodeFields(fields: Map<String, String>) = fields.entries.joinToString("&") { (key, value) -> "$key=${encodeBase64(value)}" }

  private fun encodeBase64(value: String) = Base64.encodeToString(value.toByteArray(StandardCharsets.UTF_8), Base64.URL_SAFE or Base64.NO_WRAP or Base64.NO_PADDING)

  private fun decodeBase64(value: String) = String(Base64.decode(value, Base64.URL_SAFE or Base64.NO_WRAP or Base64.NO_PADDING), StandardCharsets.UTF_8)

  private fun decodeBase64Bytes(value: String) = Base64.decode(value, Base64.URL_SAFE or Base64.NO_WRAP or Base64.NO_PADDING)

  private fun limit(fields: Map<String, String>, name: String, fallback: Long, maximum: Long): Long {
    val value = fields[name]?.toLongOrNull() ?: fallback
    require(value >= 0 && value <= maximum) { "Invalid $name" }
    return value
  }

  private fun parseHeaders(value: String): Map<String, String> =
    if (value.isEmpty()) emptyMap() else value.split(',').associate { record ->
      val fields = parseFields(decodeBase64(record))
      val name = fields.required("name")
      require(!name.any { it <= ' ' || it == ':' }) { "Invalid asset response header" }
      name to fields.required("value")
    }

  private fun normalizeAssetPath(path: String): String? {
    if (path.indexOf('\u0000') >= 0 || path.contains('\\')) return null
    val segments = path.split('/')
    if (segments.any { it.isEmpty() || it == "." || it == ".." }) return null
    return segments.joinToString("/")
  }

  private fun reasonPhrase(status: Int) = when (status) {
    400 -> "Bad Request"; 413 -> "Payload Too Large"; 429 -> "Too Many Requests"
    503 -> "Service Unavailable"; 504 -> "Gateway Timeout"; else -> "OK"
  }

  private fun isLive(state: ViewState) = attached.get() && !state.destroyed && webViews[state.handle] === state

  private fun requireView(handle: Long): ViewState = webViews[handle] ?: error("WebView handle does not exist")

  private fun operationFailed(handle: Long, operationId: String, message: String) = emit(OPERATION_FAILED, handle, message, operationId)

  private fun fullSize() = FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT)

  private fun runOnMain(action: () -> Unit): Boolean = if (Looper.myLooper() == Looper.getMainLooper()) { action(); true } else mainHandler.post(action)

  private fun requireMainThread(operation: String) {
    check(Looper.myLooper() == Looper.getMainLooper()) { "WebView host $operation must run on the main thread" }
  }

  private fun emit(kind: Int, handle: Long, payload: String, detail: String = "") {
    eventObserverForTesting?.invoke(kind, handle, payload, detail)
    NativeBridge.webViewEvent(kind, handle, payload, detail)
  }
}
