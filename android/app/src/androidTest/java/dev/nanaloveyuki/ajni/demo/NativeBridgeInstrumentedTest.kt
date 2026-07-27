package dev.nanaloveyuki.ajni.demo

import android.widget.FrameLayout
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class NativeBridgeInstrumentedTest {
  @Test
  fun unicodeStringUsesUtf16SafeJniPath() {
    NativeBridge.initialize(ApplicationProvider.getApplicationContext())
    assertEquals("JNI \u4e2d\u6587 \uD83D\uDE80", NativeBridge.echo("JNI \u4e2d\u6587 \uD83D\uDE80"))
  }

  @Test
  fun webViewHostCreatesAndDestroysAChildOnTheUiThread() {
    val instrumentation = androidx.test.platform.app.InstrumentationRegistry.getInstrumentation()
    lateinit var host: FrameLayout
    instrumentation.runOnMainSync {
      NativeBridge.initialize(ApplicationProvider.getApplicationContext())
      host = FrameLayout(ApplicationProvider.getApplicationContext())
      NativeBridge.attachWebViewContainer(host)
      assertTrue(NativeBridge.webViewCommand(AjniWebViewHost.CREATE, 7L, ""))
    }
    instrumentation.waitForIdleSync()
    instrumentation.runOnMainSync {
      assertEquals(1, host.childCount)
      assertTrue(NativeBridge.webViewCommand(AjniWebViewHost.DESTROY, 7L, ""))
    }
    instrumentation.waitForIdleSync()
    instrumentation.runOnMainSync {
      assertEquals(0, host.childCount)
      NativeBridge.detachWebViewContainer(host)
    }
  }
}
