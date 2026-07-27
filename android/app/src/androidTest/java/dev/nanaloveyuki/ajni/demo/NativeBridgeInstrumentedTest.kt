package dev.nanaloveyuki.ajni.demo

import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class NativeBridgeInstrumentedTest {
  @Test
  fun unicodeStringUsesUtf16SafeJniPath() {
    NativeBridge.initialize(ApplicationProvider.getApplicationContext())
    assertEquals("JNI \u4e2d\u6587 \uD83D\uDE80", NativeBridge.echo("JNI \u4e2d\u6587 \uD83D\uDE80"))
  }
}
