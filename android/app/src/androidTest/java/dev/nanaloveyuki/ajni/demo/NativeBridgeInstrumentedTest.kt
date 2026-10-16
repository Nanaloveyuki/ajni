package dev.nanaloveyuki.ajni.demo

import android.widget.FrameLayout
import android.util.Base64
import androidx.test.core.app.ActivityScenario
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import dev.nanaloveyuki.ajni.host.AjniWebViewHost
import dev.nanaloveyuki.ajni.host.NativeBridge
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotSame
import org.junit.Assert.assertTrue
import java.nio.charset.StandardCharsets
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicReference
import org.junit.After
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class NativeBridgeInstrumentedTest {
  private val ownedContainers = mutableListOf<FrameLayout>()

  @After
  fun cleanUpRuntime() {
    val instrumentation = androidx.test.platform.app.InstrumentationRegistry.getInstrumentation()
    var failure: Throwable? = null
    instrumentation.runOnMainSync {
      fun attempt(action: () -> Unit) {
        try {
          action()
        } catch (error: Throwable) {
          val first = failure
          if (first == null) failure = error else if (first !== error) first.addSuppressed(error)
        }
      }
      attempt { AjniWebViewHost.setEventObserverForTesting(null) }
      attempt { NativeBridge.shutdown() }
      ownedContainers.asReversed().forEach { host -> attempt { NativeBridge.detachWebViewContainer(host) } }
      attempt { ownedContainers.clear() }
    }
    failure?.let { throw it }
  }

  @Test
  fun handledObserverJavaErrorDoesNotEscapeVoidDispatch() {
    val instrumentation = androidx.test.platform.app.InstrumentationRegistry.getInstrumentation()
    instrumentation.runOnMainSync {
      NativeBridge.initialize(ApplicationProvider.getApplicationContext())
      assertTrue(NativeBridge.handledObserverErrorForTesting())
      val callback = NativeBridge.createIntCallbackForTesting()
      try {
        assertEquals(4, callback.invoke(4))
      } finally {
        callback.close()
      }
    }
  }

  @Test
  fun genericCallbackSurvivesActivityScenarioRecreation() {
    val instrumentation = androidx.test.platform.app.InstrumentationRegistry.getInstrumentation()
    ActivityScenario.launch(MainActivity::class.java).use { scenario ->
      var callback: dev.nanaloveyuki.ajni.AjniCallback? = null
      var originalActivity: MainActivity? = null
      try {
        scenario.onActivity { activity ->
          originalActivity = activity
          assertEquals(androidx.lifecycle.Lifecycle.State.RESUMED, activity.lifecycle.currentState)
          val created = NativeBridge.createIntCallbackForTesting()
          callback = created
          assertEquals(12, created.invoke(12))
        }
        scenario.recreate()
        scenario.onActivity { activity ->
          assertNotSame("ActivityScenario must replace the Activity instance", originalActivity, activity)
          assertEquals(androidx.lifecycle.Lifecycle.State.RESUMED, activity.lifecycle.currentState)
          val root = activity.findViewById<android.view.ViewGroup>(android.R.id.content).getChildAt(0)
          assertTrue("recreated Activity UI must be attached", root.isAttachedToWindow)
          val recreatedUi = root as android.widget.LinearLayout
          assertEquals(3, recreatedUi.childCount)
          assertEquals("Start native worker", (recreatedUi.getChildAt(2) as android.widget.Button).text.toString())
          NativeBridge.startWorker()
          val retained = checkNotNull(callback)
          assertEquals("the original generic session and closure state must survive recreation", 19, retained.invoke(7))
          val fresh = NativeBridge.createIntCallbackForTesting()
          try {
            assertEquals(2, fresh.invoke(2))
          } finally {
            fresh.close()
          }
          assertEquals(20, retained.invoke(1))
        }
      } finally {
        instrumentation.runOnMainSync { callback?.close() }
      }
    }
  }

  @Test
  fun genericCallbackPreservesOwnerStateAcrossFeatureRecreation() {
    val instrumentation = androidx.test.platform.app.InstrumentationRegistry.getInstrumentation()
    val context = ApplicationProvider.getApplicationContext<android.content.Context>()
    lateinit var callback: dev.nanaloveyuki.ajni.AjniCallback
    instrumentation.runOnMainSync {
      NativeBridge.initialize(context)
      callback = NativeBridge.createIntCallbackForTesting()
      assertEquals(3, callback.invoke(3))
      assertEquals(7, callback.invoke(4))
    }
    try {
      val foreignFailure = AtomicReference<Throwable?>(null)
      val worker = Thread {
        try {
          callback.invoke(100)
        } catch (error: Throwable) {
          foreignFailure.set(error)
        }
      }
      worker.start()
      worker.join(2000)
      assertFalse("callback worker blocked waiting for the owner", worker.isAlive)
      assertTrue("foreign callback must reject before managed dispatch", foreignFailure.get() is IllegalStateException)
      instrumentation.runOnMainSync {
        assertEquals(8, callback.invoke(1))
        NativeBridge.lifecycle(NativeBridge.LIFECYCLE_PAUSED)
        NativeBridge.shutdown()
        assertEquals(10, callback.invoke(2))
        NativeBridge.initialize(context)
        NativeBridge.lifecycle(NativeBridge.LIFECYCLE_RESUMED)
        assertEquals(15, callback.invoke(5))
      }
      val closeWorker = Thread { callback.close() }
      closeWorker.start()
      closeWorker.join(2000)
      assertFalse("callback close blocked waiting for the owner", closeWorker.isAlive)
      instrumentation.runOnMainSync {
        var rejected = false
        try {
          callback.invoke(1)
        } catch (_: IllegalStateException) {
          rejected = true
        }
        assertTrue("closed callback token must be rejected", rejected)
      }
    } finally {
      instrumentation.runOnMainSync { callback.close() }
    }
  }

  @Test
  fun wrongThreadNativeFeatureDispatchIsRejected() {
    val instrumentation = androidx.test.platform.app.InstrumentationRegistry.getInstrumentation()
    instrumentation.runOnMainSync { NativeBridge.initialize(ApplicationProvider.getApplicationContext()) }
    val cases = listOf(
      Triple("nativeInitialize", arrayOf<Class<*>>(android.content.Context::class.java, java.lang.Long.TYPE),
        arrayOf<Any>(ApplicationProvider.getApplicationContext<android.content.Context>(), 1L)),
      Triple("nativeLifecycle", arrayOf<Class<*>>(java.lang.Integer.TYPE), arrayOf<Any>(NativeBridge.LIFECYCLE_STARTED)),
      Triple("nativeWebViewEvent", arrayOf<Class<*>>(java.lang.Integer.TYPE, java.lang.Long.TYPE, String::class.java, String::class.java),
        arrayOf<Any>(1, 1L, "payload", "detail")),
      Triple("nativeCreateIntCallback", emptyArray<Class<*>>(), emptyArray<Any>()),
    )
    for ((name, types, arguments) in cases) {
      val method = NativeBridge::class.java.getDeclaredMethod(name, *types)
      method.isAccessible = true
      var rejected = false
      try {
        method.invoke(null, *arguments)
      } catch (error: java.lang.reflect.InvocationTargetException) {
        rejected = error.cause is IllegalStateException
      }
      assertTrue("$name must reject non-owner JNI entry", rejected)
    }
  }

  @Test
  fun unicodeStringUsesUtf16SafeJniPath() {
    val instrumentation = androidx.test.platform.app.InstrumentationRegistry.getInstrumentation()
    instrumentation.runOnMainSync {
      NativeBridge.initialize(ApplicationProvider.getApplicationContext())
    }
    assertEquals("JNI \u4e2d\u6587 \uD83D\uDE80", NativeBridge.echo("JNI \u4e2d\u6587 \uD83D\uDE80"))
  }

  @Test
  fun webViewHostCreatesAndDestroysAChildOnTheUiThread() {
    val instrumentation = androidx.test.platform.app.InstrumentationRegistry.getInstrumentation()
    lateinit var host: FrameLayout
    instrumentation.runOnMainSync {
      NativeBridge.initialize(ApplicationProvider.getApplicationContext())
      host = FrameLayout(ApplicationProvider.getApplicationContext())
      ownedContainers.add(host)
      NativeBridge.attachWebViewContainer(host)
      assertTrue(NativeBridge.webViewCommand(AjniWebViewHost.CREATE, 7L, "", ""))
    }
    instrumentation.waitForIdleSync()
    instrumentation.runOnMainSync {
      assertEquals(1, host.childCount)
      assertTrue(NativeBridge.webViewCommand(AjniWebViewHost.DESTROY, 7L, "", ""))
    }
    instrumentation.waitForIdleSync()
    instrumentation.runOnMainSync {
      assertEquals(0, host.childCount)
    }
  }

  @Test
  fun webViewHostPausesAndRecreatesAcrossContainers() {
    val instrumentation = androidx.test.platform.app.InstrumentationRegistry.getInstrumentation()
    val context = ApplicationProvider.getApplicationContext<android.content.Context>()
    val destroyed = CountDownLatch(1)
    val observerFailure = AtomicReference<Throwable?>(null)
    val firstHandle = 81L
    val secondHandle = 82L
    lateinit var firstHost: FrameLayout
    lateinit var secondHost: FrameLayout
    instrumentation.runOnMainSync {
      AjniWebViewHost.setEventObserverForTesting { kind, handle, _, _ ->
        try {
          if (kind == AjniWebViewHost.DESTROYED && handle == firstHandle) destroyed.countDown()
        } catch (error: Throwable) {
          observerFailure.compareAndSet(null, error)
          destroyed.countDown()
        }
      }
      NativeBridge.initialize(context)
      firstHost = FrameLayout(context)
      ownedContainers.add(firstHost)
      NativeBridge.attachWebViewContainer(firstHost)
      assertTrue(NativeBridge.webViewCommand(AjniWebViewHost.CREATE, firstHandle, "", "create-first"))
    }
    instrumentation.waitForIdleSync()
    instrumentation.runOnMainSync {
      assertEquals(1, firstHost.childCount)
      NativeBridge.lifecycle(NativeBridge.LIFECYCLE_PAUSED)
      assertTrue(AjniWebViewHost.isWebViewPausedForTesting(firstHandle))
      assertTrue(NativeBridge.webViewCommand(AjniWebViewHost.CREATE, 83L, "", "create-paused"))
      assertTrue(AjniWebViewHost.isWebViewPausedForTesting(83L))
      NativeBridge.lifecycle(NativeBridge.LIFECYCLE_RESUMED)
      assertFalse(AjniWebViewHost.isWebViewPausedForTesting(firstHandle))
      assertFalse(AjniWebViewHost.isWebViewPausedForTesting(83L))
      secondHost = FrameLayout(context)
      ownedContainers.add(secondHost)
      NativeBridge.attachWebViewContainer(secondHost)
      assertEquals(0, firstHost.childCount)
      assertTrue(NativeBridge.webViewCommand(AjniWebViewHost.CREATE, secondHandle, "", "create-second"))
    }
    val wasDestroyed = destroyed.await(2, TimeUnit.SECONDS)
    observerFailure.get()?.let { throw it }
    assertTrue("old WebView was not destroyed during container recreation", wasDestroyed)
    instrumentation.waitForIdleSync()
    instrumentation.runOnMainSync {
      assertEquals(1, secondHost.childCount)
    }
  }

  @Test
  fun secureWebViewHostObserverSeesPageMessageAndCompletesAssetRequest() {
    val instrumentation = androidx.test.platform.app.InstrumentationRegistry.getInstrumentation()
    val context = ApplicationProvider.getApplicationContext<android.content.Context>()
    val pageMessage = CountDownLatch(1)
    val assetRequest = CountDownLatch(1)
    val operationFailure = CountDownLatch(1)
    val observerFailure = AtomicReference<Throwable?>(null)
    val handle = 41L
    lateinit var host: FrameLayout
    instrumentation.runOnMainSync {
      AjniWebViewHost.setEventObserverForTesting { kind, eventHandle, payload, _ ->
        if (eventHandle != handle || observerFailure.get() != null) return@setEventObserverForTesting
        try {
          when (kind) {
            AjniWebViewHost.PAGE_MESSAGE -> {
              assertEquals("body=aGVsbG8&origin=aHR0cHM6Ly9vcmJpdC50ZXN0&main_frame=MQ", payload)
              pageMessage.countDown()
            }
            AjniWebViewHost.ASSET_REQUEST -> {
              val requestId = decodeFields(payload).getValue("request_id")
              assertEquals("style.css", decodeFields(payload).getValue("path"))
              val header = field("name", "Cache-Control") + "&" + field("value", "no-store")
              val response = listOf(
                field("request_id", requestId),
                field("status", "200"),
                field("mime", "text/css"),
                field("encoding", "utf-8"),
                field("headers", b64(header)),
                field("body", b64("body { background: rgb(1, 2, 3); }")),
              ).joinToString("&")
              assertTrue(NativeBridge.webViewCommand(AjniWebViewHost.RESPOND_ASSET, handle, response, "asset-response"))
              assetRequest.countDown()
            }
            AjniWebViewHost.OPERATION_FAILED -> operationFailure.countDown()
          }
        } catch (error: Throwable) {
          observerFailure.compareAndSet(null, error)
          pageMessage.countDown()
          assetRequest.countDown()
          operationFailure.countDown()
        }
      }
      NativeBridge.initialize(context)
      host = FrameLayout(context)
      ownedContainers.add(host)
      NativeBridge.attachWebViewContainer(host)
      val script = "window.ajni.postMessage('hello');"
      val options = listOf(
        field("origin", "https://orbit.test"),
        field("initial_kind", "html"),
        field("initial", "<html><head><link rel=\"stylesheet\" href=\"style.css\"></head><body>ok</body></html>"),
        field("base_url", "https://orbit.test/assets/index.html"),
        field("scripts", b64(script)),
      ).joinToString("&")
      assertTrue(NativeBridge.webViewCommand(AjniWebViewHost.CREATE_WITH_OPTIONS, handle, options, "create"))
    }
    val sawPageMessage = pageMessage.await(10, TimeUnit.SECONDS)
    observerFailure.get()?.let { throw it }
    assertTrue("page message was not delivered", sawPageMessage)
    val sawAssetRequest = assetRequest.await(10, TimeUnit.SECONDS)
    observerFailure.get()?.let { throw it }
    assertTrue("asset request was not delivered", sawAssetRequest)
    val sawOperationFailure = operationFailure.await(300, TimeUnit.MILLISECONDS)
    observerFailure.get()?.let { throw it }
    assertFalse("unexpected WebView operation failure", sawOperationFailure)
    instrumentation.runOnMainSync {
      assertTrue(NativeBridge.webViewCommand(AjniWebViewHost.DESTROY, handle, "", "destroy"))
    }
  }

  private fun field(name: String, value: String): String = "$name=${b64(value)}"

  private fun b64(value: String): String =
    Base64.encodeToString(value.toByteArray(StandardCharsets.UTF_8), Base64.URL_SAFE or Base64.NO_WRAP or Base64.NO_PADDING)

  private fun decodeFields(payload: String): Map<String, String> = payload.split('&').associate { part ->
    val separator = part.indexOf('=')
    part.substring(0, separator) to String(
      Base64.decode(part.substring(separator + 1), Base64.URL_SAFE or Base64.NO_WRAP or Base64.NO_PADDING),
      StandardCharsets.UTF_8,
    )
  }
}
