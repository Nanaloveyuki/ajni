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
import android.webkit.RenderProcessGoneDetail
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
import java.util.concurrent.TimeoutException
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
  private val pendingCreations = ConcurrentHashMap<Long, Any>()
  private val pendingAssets = ConcurrentHashMap<String, PendingAsset>()
  private val nextAssetId = AtomicLong(1)
  @Volatile private var container: FrameLayout? = null
  @Volatile private var hostGeneration = 0L
  private var hostPaused = false
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
    var assetLoader: WebViewAssetLoader?,
    val limits: ResourceLimits,
    val creationIdentity: Any?,
    val runtimeGeneration: Long = NativeBridge.currentRuntimeGeneration(),
    val hostGeneration: Long = AjniWebViewHost.hostGeneration,
  ) {
    val scripts = mutableListOf<ScriptHandler>()
    var currentNavigationOperation = ""
    var paused = false
    @Volatile var destroyed = false
  }

  private data class AssetResponse(
    val status: Int,
    val mime: String,
    val encoding: String?,
    val headers: Map<String, String>,
    val body: ByteArray,
  )

  private data class PendingAsset(val state: ViewState, val future: CompletableFuture<AssetResponse>)

  fun attach(host: FrameLayout) {
    requireMainThread("attach")
    attached.set(false)
    val generation = invalidateHost()
    destroyAll()
    if (hostGeneration != generation) return
    container = host
    attached.set(true)
  }

  fun detach(host: FrameLayout) {
    val generation = hostGeneration
    if (Looper.myLooper() != Looper.getMainLooper()) {
      mainHandler.post { if (hostGeneration == generation) detach(host) }
      return
    }
    if (container !== host) return
    attached.set(false)
    invalidateHost()
    container = null
    destroyAll()
  }

  internal fun runtimeShutdown() {
    requireMainThread("runtimeShutdown")
    invalidateHost()
    hostPaused = false
    completePendingAssets(null, unavailableAssetResponse(503, "Native runtime unavailable"))
  }

  private fun invalidateHost(): Long = synchronized(pendingCreations) {
    pendingCreations.clear()
    synchronized(pendingAssets) { ++hostGeneration }
  }

  fun pause() {
    requireMainThread("pause")
    hostPaused = true
    webViews.values.forEach { state ->
      if (isLive(state) && !state.paused) {
        state.view.onPause()
        state.paused = true
      }
    }
  }

  fun resume() {
    requireMainThread("resume")
    hostPaused = false
    webViews.values.forEach { state ->
      if (isLive(state) && state.paused) {
        state.view.onResume()
        state.paused = false
      }
    }
  }

  fun command(command: Int, handle: Long, payload: String, requestId: String): Boolean =
    commandForRuntime(command, handle, payload, requestId, NativeBridge.currentRuntimeGeneration())

  internal fun commandForRuntime(command: Int, handle: Long, payload: String, requestId: String, runtime: Long): Boolean = synchronized(pendingCreations) {
    val generation = hostGeneration
    if (!isCurrentHost(generation, runtime)) return false
    val expected = webViews[handle]
    val creates = command == CREATE || command == CREATE_WITH_OPTIONS
    val creation = if (creates && expected == null) pendingCreations.getOrPut(handle) { Any() } else pendingCreations[handle]
    val accepted = runOnMain {
      synchronized(pendingCreations) {
        try {
          if (!isCurrentHost(generation, runtime) || !matchesQueuedView(handle, expected, creation, creates)) return@runOnMain
          try {
            when (command) {
              CREATE -> createLegacy(handle, payload, requestId, creation)
              CREATE_WITH_OPTIONS -> createWithOptions(handle, parseFields(payload), requestId, creation)
              NAVIGATE -> navigate(handle, payload, requestId)
              LOAD_HTML -> loadHtml(handle, payload, null, requestId)
              LOAD_HTML_WITH_BASE_URL -> {
                val fields = parseFields(payload)
                loadHtml(handle, fields.required("html"), fields.required("base_url"), requestId)
              }
              EVAL -> {
                val state = requireView(handle)
                state.view.evaluateJavascript(payload) {
                  if (isLive(state)) emitForState(state, SCRIPT_RESULT, it ?: "null", requestId)
                }
              }
              POST_MESSAGE -> postMessage(handle, parseFields(payload).required("body"))
              RESPOND_ASSET -> respondAsset(handle, parseFields(payload), requestId)
              ADD_DOCUMENT_START_SCRIPT -> addDocumentStartScript(handle, parseFields(payload).required("script"))
              DESTROY -> destroy(handle, requestId)
              else -> operationFailed(handle, requestId, "Unsupported WebView command: $command")
            }
          } catch (error: Exception) {
            Log.e(TAG, "WebView command $command failed for handle $handle", error)
            if (isCurrentHost(generation, runtime)) {
              operationFailed(handle, requestId, error.message ?: error.javaClass.simpleName)
            }
          }
        } finally {
          if (creates && creation != null) pendingCreations.remove(handle, creation)
        }
      }
    }
    if (!accepted && creates && creation != null) pendingCreations.remove(handle, creation)
    accepted
  }

  fun setBounds(handle: Long, x: Int, y: Int, width: Int, height: Int): Boolean =
    setBoundsForRuntime(handle, x, y, width, height, NativeBridge.currentRuntimeGeneration())

  internal fun setBoundsForRuntime(handle: Long, x: Int, y: Int, width: Int, height: Int, runtime: Long): Boolean = synchronized(pendingCreations) {
    val generation = hostGeneration
    val expected = webViews[handle]
    val creation = pendingCreations[handle]
    if (!isCurrentHost(generation, runtime) || width < 1 || height < 1) return false
    runOnMain {
      synchronized(pendingCreations) {
        if (!isCurrentHost(generation, runtime) || !matchesQueuedView(handle, expected, creation)) return@runOnMain
        try {
          requireView(handle).view.layoutParams = FrameLayout.LayoutParams(width, height).apply {
            leftMargin = x
            topMargin = y
          }
        } catch (error: Exception) {
          Log.e(TAG, "WebView bounds update failed for handle $handle", error)
          if (isCurrentHost(generation, runtime)) emit(FAILED, handle, error.message ?: error.javaClass.simpleName)
        }
      }
    }
  }

  private fun matchesQueuedView(handle: Long, expected: ViewState?, creation: Any?, creates: Boolean = false): Boolean {
    val current = webViews[handle]
    return if (expected != null) current === expected
    else if (current != null) creation != null && current.creationIdentity === creation
    else creation == null || (creates && pendingCreations[handle] === creation)
  }

  @VisibleForTesting
  fun setEventObserverForTesting(observer: ((Int, Long, String, String) -> Unit)?) {
    eventObserverForTesting = observer
  }

  @VisibleForTesting
  fun isWebViewPausedForTesting(handle: Long): Boolean = webViews[handle]?.paused ?: false

  private fun createLegacy(handle: Long, initialUrl: String, operationId: String, creation: Any?) {
    check(!webViews.containsKey(handle)) { "WebView handle already exists" }
    val host = requireNotNull(container)
    val state = allocateState(host, handle, null, defaultLimits(), creation)
    val view = state.view
    try {
      configureWebView(view)
      configureClients(state)
      webViews[handle] = state
      host.addView(view, fullSize())
      inheritPause(state)
      if (initialUrl.isNotEmpty()) navigate(handle, initialUrl, operationId)
      check(isLive(state)) { "WebView creation was invalidated" }
    } catch (error: Throwable) {
      cleanupState(state)?.let(error::addSuppressed)
      throw error
    }
    emitForState(state, CREATED, initialUrl, operationId)
  }

  private fun createWithOptions(handle: Long, fields: Map<String, String>, operationId: String, creation: Any?) {
    check(!webViews.containsKey(handle)) { "WebView handle already exists" }
    val options = parseCreateOptions(fields)
    requireWebKitFeatures()
    val host = requireNotNull(container)
    val state = allocateState(host, handle, options.origin, options.limits, creation)
    val view = state.view
    try {
      configureWebView(view)
      state.assetLoader = WebViewAssetLoader.Builder()
        .setDomain(requireNotNull(options.origin.host))
        .addPathHandler(ASSET_PREFIX, DynamicAssetPathHandler(state))
        .build()
      configureSecureChannel(state)
      configureClients(state)
      options.scripts.forEach { addScript(state, it) }
      webViews[handle] = state
      host.addView(view, fullSize())
      inheritPause(state)
      when (options.initialKind) {
        "empty" -> Unit
        "url" -> navigate(handle, options.initial, operationId)
        "html" -> loadHtml(handle, options.initial, options.baseUrl, operationId)
      }
      check(isLive(state)) { "WebView creation was invalidated" }
    } catch (error: Throwable) {
      cleanupState(state)?.let(error::addSuppressed)
      throw error
    }
    emitForState(state, CREATED, options.initial, operationId)
  }

  private fun allocateState(host: FrameLayout, handle: Long, origin: Uri?, limits: ResourceLimits, creation: Any?): ViewState {
    val view = WebView(host.context)
    try {
      return ViewState(handle, view, origin, null, limits, creation)
    } catch (error: Throwable) {
      try {
        view.destroy()
      } catch (cleanupError: Throwable) {
        if (cleanupError !== error) error.addSuppressed(cleanupError)
      }
      throw error
    }
  }

  private fun inheritPause(state: ViewState) {
    if (hostPaused) {
      state.view.onPause()
      state.paused = true
    }
  }

  @SuppressLint("SetJavaScriptEnabled")
  private fun configureWebView(view: WebView) = with(view) {
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
      emitForState(state, PAGE_MESSAGE, encodeFields(mapOf(
        "body" to body,
        "origin" to sourceOrigin.toString(),
        "main_frame" to if (isMainFrame) "1" else "0",
      )))
    }
  }

  private fun configureClients(state: ViewState) {
    state.view.webViewClient = object : WebViewClient() {
      override fun onPageStarted(view: WebView, url: String?, favicon: android.graphics.Bitmap?) {
        if (isLive(state)) emitForState(state, NAVIGATION_STARTED, url.orEmpty(), state.currentNavigationOperation)
      }

      override fun onPageFinished(view: WebView, url: String?) {
        if (isLive(state)) emitForState(state, NAVIGATION_FINISHED, url.orEmpty(), state.currentNavigationOperation)
      }

      override fun onReceivedError(view: WebView, request: WebResourceRequest, error: WebResourceError) {
        if (isLive(state) && request.isForMainFrame) {
          emitForState(state, OPERATION_FAILED, error.description?.toString().orEmpty(), state.currentNavigationOperation)
        }
      }

      override fun shouldInterceptRequest(view: WebView, request: WebResourceRequest): WebResourceResponse? =
        if (isLive(state)) state.assetLoader?.shouldInterceptRequest(request.url)
        else unavailableAssetResponse(503, "WebView destroyed").toWebResponse()

      @android.annotation.TargetApi(Build.VERSION_CODES.O)
      override fun onRenderProcessGone(view: WebView, detail: RenderProcessGoneDetail): Boolean {
        if (view !== state.view) return false
        if (state.destroyed) return true
        val generation = hostGeneration
        val wasLive = isLive(state)
        val message = if (detail.didCrash()) "WebView renderer crashed" else "WebView renderer terminated"
        cleanupState(state)?.let { Log.e(TAG, "Renderer cleanup failed for handle ${state.handle}", it) }
        if (wasLive) {
          emitTerminatedState(state, FAILED, message, generation = generation)
          emitTerminatedState(state, DESTROYED, "", generation = generation)
        }
        return true
      }
    }
    state.view.webChromeClient = object : WebChromeClient() {
      override fun onReceivedTitle(view: WebView, title: String?) {
        if (isLive(state)) emitForState(state, TITLE_CHANGED, title.orEmpty())
      }
    }
  }

  private fun navigate(handle: Long, url: String, operationId: String) {
    val state = requireView(handle)
    state.origin?.let { validateAssetUrl(it, url, "Navigation must remain within the configured asset origin") }
    state.currentNavigationOperation = operationId
    state.view.loadUrl(url)
  }

  private fun loadHtml(handle: Long, html: String, baseUrl: String?, operationId: String) {
    val state = requireView(handle)
    if (baseUrl != null && state.origin != null) {
      validateAssetUrl(state.origin, baseUrl, "HTML base URL must use the configured asset origin")
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
    val pending = pendingAssets[requestId]
    if (pending == null || pending.state.handle != handle || webViews[handle] !== pending.state) {
      operationFailed(handle, operationId, "Unknown asset request")
      return
    }
    val state = pending.state
    if (!isLive(state)) {
      finishPendingAsset(requestId, pending, unavailableAssetResponse(503, "WebView destroyed"))
      return
    }
    val response = try {
      val status = fields.required("status").toIntOrNull()?.takeIf { it in 100..299 || it in 400..599 }
        ?: throw IllegalArgumentException("Asset response status must be 100..299 or 400..599")
      val mime = fields.required("mime")
      require(!mime.any { it < ' ' || it == '\u007f' }) { "Invalid asset response MIME type" }
      val encoding = fields["encoding"]?.takeIf(String::isNotEmpty)
      require(encoding == null || !encoding.any { it < ' ' || it == '\u007f' }) { "Invalid asset response encoding" }
      val headers = parseHeaders(fields["headers"].orEmpty())
      val body = decodeBase64Bytes(fields.required("body"))
      if (state.limits.maxAssetResponseBytes > 0 && body.size.toLong() > state.limits.maxAssetResponseBytes) {
        finishPendingAsset(requestId, pending, unavailableAssetResponse(413, "Asset response exceeds configured size limit"))
        operationFailed(handle, operationId, "Asset response exceeds configured size limit")
        return
      }
      AssetResponse(status, mime, encoding, headers, body)
    } catch (error: Exception) {
      finishPendingAsset(requestId, pending, unavailableAssetResponse(400, "Invalid asset response"))
      throw error
    }
    if (!finishPendingAsset(requestId, pending, response)) {
      operationFailed(handle, operationId, "Unknown asset request")
    }
  }

  private fun destroy(handle: Long, operationId: String = "") {
    val generation = hostGeneration
    val state = webViews[handle]
    if (state == null) {
      emit(DESTROYED, handle, "", operationId)
      return
    }
    val failure = cleanupState(state)
    if (failure != null) emitTerminatedState(state, OPERATION_FAILED, failure.message ?: failure.javaClass.simpleName, operationId, generation)
    emitTerminatedState(state, DESTROYED, "", operationId, generation)
  }

  private fun cleanupState(state: ViewState): Throwable? {
    synchronized(pendingAssets) {
      if (state.destroyed) return null
      state.destroyed = true
      webViews.remove(state.handle, state)
    }
    completePendingAssets(state, unavailableAssetResponse(503, "WebView destroyed"))
    var failure: Throwable? = null
    fun cleanup(action: () -> Unit) {
      try {
        action()
      } catch (error: Throwable) {
        val previous = failure
        if (previous == null) failure = error else if (previous !== error) previous.addSuppressed(error)
      }
    }
    if (state.origin != null) cleanup {
      if (WebViewFeature.isFeatureSupported(WebViewFeature.WEB_MESSAGE_LISTENER)) {
        WebViewCompat.removeWebMessageListener(state.view, MESSAGE_LISTENER_NAME)
      }
    }
    state.scripts.forEach { script -> cleanup { script.remove() } }
    state.scripts.clear()
    cleanup { (state.view.parent as? ViewGroup)?.removeView(state.view) }
    cleanup { state.view.destroy() }
    return failure
  }

  private fun destroyAll() {
    val generation = hostGeneration
    val states = webViews.values.toList()
    val failures = states.map { cleanupState(it) }
    states.forEachIndexed { index, state ->
      failures[index]?.let {
        Log.e(TAG, "WebView cleanup failed for handle ${state.handle}", it)
        emitTerminatedState(state, OPERATION_FAILED, it.message ?: it.javaClass.simpleName, generation = generation)
      }
      emitTerminatedState(state, DESTROYED, "", generation = generation)
    }
  }

  private class DynamicAssetPathHandler(private val state: ViewState) : WebViewAssetLoader.PathHandler {
    override fun handle(path: String): WebResourceResponse {
      if (!isLive(state)) return unavailableAssetResponse(503, "WebView destroyed").toWebResponse()
      val normalized = normalizeAssetPath(path) ?: return unavailableAssetResponse(400, "Invalid asset path").toWebResponse()
      val id = "${state.handle}:${nextAssetId.getAndIncrement()}"
      val future = CompletableFuture<AssetResponse>()
      val pending = PendingAsset(state, future)
      if (!admitPendingAsset(id, pending, state.limits.maxPendingAssetRequests)) {
        val status = if (isLive(state)) 429 else 503
        val response = unavailableAssetResponse(status, if (status == 429) "Too many pending asset requests" else "WebView destroyed")
        future.complete(response)
        return response.toWebResponse()
      }
      try {
        if (!mainHandler.post {
            try {
              if (isLive(state) && pendingAssets[id] === pending) {
                emitForState(state, ASSET_REQUEST, encodeFields(mapOf("request_id" to id, "path" to normalized)))
              } else {
                finishPendingAsset(id, pending, unavailableAssetResponse(503, "WebView destroyed"))
              }
            } catch (error: Throwable) {
              finishPendingAsset(id, pending, unavailableAssetResponse(500, "Asset request dispatch failed"))
              if (error !is Exception) throw error
              Log.e(TAG, "Asset request dispatch failed for handle ${state.handle}", error)
            }
          }) {
          val response = unavailableAssetResponse(503, "Native runtime unavailable")
          finishPendingAsset(id, pending, response)
          return response.toWebResponse()
        }
        val response = if (state.limits.assetResponseTimeoutMillis == 0L) future.get()
        else future.get(state.limits.assetResponseTimeoutMillis, TimeUnit.MILLISECONDS)
        return response.toWebResponse()
      } catch (_: TimeoutException) {
        val response = unavailableAssetResponse(504, "Asset response timed out")
        finishPendingAsset(id, pending, response)
        return response.toWebResponse()
      } catch (_: InterruptedException) {
        val response = unavailableAssetResponse(503, "Asset response interrupted")
        finishPendingAsset(id, pending, response)
        Thread.currentThread().interrupt()
        return response.toWebResponse()
      } catch (error: Exception) {
        val response = unavailableAssetResponse(500, "Asset response failed")
        finishPendingAsset(id, pending, response)
        Log.e(TAG, "Asset response failed for handle ${state.handle}", error)
        return response.toWebResponse()
      } finally {
        finishPendingAsset(id, pending, unavailableAssetResponse(500, "Asset request terminated"))
      }
    }
  }

  private fun AssetResponse.toWebResponse() = WebResourceResponse(
    mime.ifEmpty { "application/octet-stream" }, encoding, status, reasonPhrase(status), headers, ByteArrayInputStream(body),
  )

  private fun unavailableAssetResponse(status: Int, message: String) = AssetResponse(
    status, "text/plain", "utf-8", mapOf("Cache-Control" to "no-store"), message.toByteArray(StandardCharsets.UTF_8),
  )

  private fun completePendingAssets(state: ViewState?, response: AssetResponse) {
    pendingAssets.entries.toList().forEach { (id, pending) ->
      if (state == null || pending.state === state) finishPendingAsset(id, pending, response)
    }
  }

  private fun finishPendingAsset(id: String, pending: PendingAsset, response: AssetResponse): Boolean = synchronized(pendingAssets) {
    val owned = pendingAssets[id] === pending
    // Completion precedes removal, so absent entries never hide a non-terminal future.
    pending.future.complete(response)
    if (owned) pendingAssets.remove(id, pending)
    owned
  }

  private fun admitPendingAsset(id: String, pending: PendingAsset, limit: Int): Boolean = synchronized(pendingAssets) {
    if (!isLive(pending.state) || (limit > 0 && pendingAssets.values.count { it.state === pending.state } >= limit)) false
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
    when (initialKind) {
      "url" -> validateAssetUrl(origin, initial, "Navigation must remain within the configured asset origin")
      "html" -> validateAssetUrl(origin, baseUrl, "HTML base URL must use the configured asset origin")
    }
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

  private fun validateAssetUrl(origin: Uri, value: String, message: String) {
    val destination = Uri.parse(value)
    require(destination.scheme == "https" && destination.host == origin.host && destination.port == -1 &&
      destination.userInfo == null && destination.path?.startsWith(ASSET_PREFIX) == true) { message }
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
      require(name.isNotEmpty() && name.all { it in 'a'..'z' || it in 'A'..'Z' || it in '0'..'9' || it in "!#$%&'*+-.^_`|~" }) {
        "Invalid asset response header"
      }
      val headerValue = fields.required("value")
      require(!headerValue.any { (it < ' ' && it != '\t') || it == '\u007f' }) { "Invalid asset response header value" }
      name to headerValue
    }

  private fun normalizeAssetPath(path: String): String? {
    if (path.indexOf('\u0000') >= 0 || path.contains('\\')) return null
    val segments = path.split('/')
    if (segments.any { it.isEmpty() || it == "." || it == ".." }) return null
    return segments.joinToString("/")
  }

  private fun reasonPhrase(status: Int) = when (status) {
    400 -> "Bad Request"; 413 -> "Payload Too Large"; 429 -> "Too Many Requests"
    500 -> "Internal Server Error"; 503 -> "Service Unavailable"; 504 -> "Gateway Timeout"; else -> "Response"
  }

  private fun isCurrentHost(generation: Long, runtime: Long) =
    attached.get() && container != null && hostGeneration == generation && NativeBridge.matchesRuntimeGeneration(runtime)

  private fun isLive(state: ViewState) =
    !state.destroyed && webViews[state.handle] === state && isCurrentHost(state.hostGeneration, state.runtimeGeneration)

  private fun requireView(handle: Long): ViewState {
    val state = webViews[handle] ?: error("WebView handle does not exist")
    check(isLive(state)) { "WebView handle belongs to an inactive host" }
    return state
  }

  private fun operationFailed(handle: Long, operationId: String, message: String) = emit(OPERATION_FAILED, handle, message, operationId)

  private fun fullSize() = FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT)

  private fun runOnMain(action: () -> Unit): Boolean = if (Looper.myLooper() == Looper.getMainLooper()) { action(); true } else mainHandler.post(action)

  private fun requireMainThread(operation: String) {
    check(Looper.myLooper() == Looper.getMainLooper()) { "WebView host $operation must run on the main thread" }
  }

  private fun emitForState(state: ViewState, kind: Int, payload: String, detail: String = "") {
    if (!isLive(state)) return
    eventObserverForTesting?.invoke(kind, state.handle, payload, detail)
    if (isLive(state)) NativeBridge.webViewEventForRuntime(kind, state.handle, payload, detail, state.runtimeGeneration)
  }

  internal fun eventIdentityCheck(handle: Long): () -> Boolean {
    val generation = hostGeneration
    val state = webViews[handle]
    return { hostGeneration == generation && webViews[handle] === state && (state == null || isLive(state)) }
  }

  private fun emitTerminatedState(state: ViewState, kind: Int, payload: String, detail: String = "", generation: Long = hostGeneration) {
    if (hostGeneration != generation || !NativeBridge.matchesRuntimeGeneration(state.runtimeGeneration) || webViews.containsKey(state.handle)) return
    eventObserverForTesting?.invoke(kind, state.handle, payload, detail)
    if (hostGeneration == generation && !webViews.containsKey(state.handle)) {
      NativeBridge.webViewEventForRuntime(kind, state.handle, payload, detail, state.runtimeGeneration)
    }
  }

  private fun emit(kind: Int, handle: Long, payload: String, detail: String = "") {
    val runtime = NativeBridge.currentRuntimeGeneration()
    val generation = hostGeneration
    val state = webViews[handle]
    if (!NativeBridge.matchesRuntimeGeneration(runtime)) return
    eventObserverForTesting?.invoke(kind, handle, payload, detail)
    if (hostGeneration == generation && webViews[handle] === state) {
      NativeBridge.webViewEventForRuntime(kind, handle, payload, detail, runtime)
    }
  }
}
