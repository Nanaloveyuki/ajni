package dev.nanaloveyuki.ajni.demo

import android.content.Context
import android.content.ContextWrapper
import androidx.test.core.app.ActivityScenario
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import dev.nanaloveyuki.ajni.host.NativeBridge
import java.lang.reflect.InvocationTargetException
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class ApplicationContextInstrumentedTest {
  @After
  fun stopFeature() {
    InstrumentationRegistry.getInstrumentation().runOnMainSync { NativeBridge.shutdown() }
  }

  @Test
  fun checkedContextHandlesFollowFeatureAndSessionLifetimes() {
    val instrumentation = InstrumentationRegistry.getInstrumentation()
    instrumentation.runOnMainSync { NativeBridge.shutdown() }
    ActivityScenario.launch(MainActivity::class.java).use { scenario ->
      scenario.onActivity { activity ->
        val application = activity.applicationContext
        val packageName = application.packageName
        val callback = NativeBridge.createIntCallbackForTesting()
        try {
          // Stage zero also checks the acquisition attempted during actual package initialization.
          assertSame(application, NativeBridge.applicationContextForTesting(0))
          assertSame(application, NativeBridge.applicationContextForTesting(1))
          assertEquals(packageName, NativeBridge.contextPackageNameForTesting())
          assertEquals(3, callback.invoke(3))
          NativeBridge.lifecycle(NativeBridge.LIFECYCLE_RESUMED)
          NativeBridge.shutdown()
          val stopped = failure { NativeBridge.applicationContextForTesting(2) }
          assertEquals(IllegalStateException::class.java, stopped.javaClass)
          assertEquals("NativeBridge is not initialized", stopped.message)
          assertTrue("the original getter Throwable must survive JavaThrown export",
            stopped.stackTrace.any { it.methodName == "applicationContextForRuntime" })
          assertEquals(packageName, NativeBridge.contextPackageNameForTesting())
          assertEquals(7, callback.invoke(4))
          NativeBridge.initialize(activity)
          NativeBridge.lifecycle(NativeBridge.LIFECYCLE_RESUMED)
          assertSame(application, NativeBridge.applicationContextForTesting(5))
          assertEquals(packageName, NativeBridge.contextPackageNameForTesting())
          assertEquals(12, callback.invoke(5))
          callback.close()

          // The C helper closes directly, outside a managed entry; reopen advances session generation.
          NativeBridge.contextCloseSessionForTesting()
          NativeBridge.initialize(activity)
          assertSame(application, NativeBridge.applicationContextForTesting(3))
          assertEquals(packageName, NativeBridge.contextPackageNameForTesting())
        } finally {
          callback.close()
          NativeBridge.initialize(application)
          NativeBridge.applicationContextForTesting(4)
        }
      }
    }
  }

  @Test
  fun normalizationAndNativeInitializationFailuresRollBackPublication() {
    val instrumentation = InstrumentationRegistry.getInstrumentation()
    val application = ApplicationProvider.getApplicationContext<Context>()
    instrumentation.runOnMainSync {
      NativeBridge.shutdown()
      val normalizationError = UnsupportedOperationException("normalization sentinel")
      val throwingNormalization = object : ContextWrapper(application) {
        override fun getApplicationContext(): Context = throw normalizationError
      }
      val nullApplication = object : ContextWrapper(application) {
        override fun getApplicationContext(): Context? = null
      }
      val loaderError = UnsupportedOperationException("class loader sentinel")
      var failLoader = true
      val normalized = object : ContextWrapper(application) {
        override fun getApplicationContext(): Context = this
        override fun getClassLoader(): ClassLoader {
          if (failLoader) throw loaderError
          return super.getClassLoader()
        }
      }
      assertSame(normalizationError, failure { NativeBridge.initialize(throwingNormalization) })
      assertTrue(failure { NativeBridge.initialize(nullApplication) } is IllegalArgumentException)
      assertSame(loaderError, failure { NativeBridge.initialize(normalized) })

      // Successful initialization after each failure proves transitions were rolled back.
      failLoader = false
      NativeBridge.initialize(normalized)
      assertSame(normalized, NativeBridge.applicationContextForTesting(2))
      assertSame(normalizationError, failure { NativeBridge.initialize(throwingNormalization) })
      assertTrue(failure { NativeBridge.initialize(nullApplication) } is IllegalArgumentException)
      assertTrue(failure { NativeBridge.initialize(application) } is IllegalStateException)
      failLoader = true
      assertSame(loaderError, failure { NativeBridge.initialize(normalized) })
      assertSame("failed/conflicting initialization must preserve the prior Context",
        normalized, NativeBridge.applicationContextForTesting(2))
      failLoader = false
      NativeBridge.initialize(normalized)
      assertSame(normalized, NativeBridge.applicationContextForTesting(2))
      NativeBridge.shutdown()
      NativeBridge.initialize(application)
      assertSame(application, NativeBridge.applicationContextForTesting(2))
    }
  }

  @Test
  fun foreignThreadContextEntryIsRejectedBeforeManagedDispatch() {
    val instrumentation = InstrumentationRegistry.getInstrumentation()
    instrumentation.runOnMainSync {
      NativeBridge.initialize(ApplicationProvider.getApplicationContext())
    }
    val native = NativeBridge::class.java.getDeclaredMethod(
      "nativeApplicationContextForTesting", java.lang.Integer.TYPE)
    native.isAccessible = true
    // The test runner is a foreign thread; reflection bypasses the Kotlin wrapper's own guard.
    val rejected = failure { native.invoke(null, 2) }
    assertTrue(rejected is InvocationTargetException)
    assertTrue(rejected.cause is IllegalStateException)
    assertFalse("native rejection must happen before public getter execution",
      rejected.cause!!.stackTrace.any { it.methodName == "applicationContextForRuntime" })
    instrumentation.runOnMainSync {
      assertSame(ApplicationProvider.getApplicationContext<Context>(), NativeBridge.applicationContextForTesting(2))
    }
  }

  private fun failure(action: () -> Unit): Throwable {
    try {
      action()
    } catch (error: Throwable) {
      return error
    }
    throw AssertionError("Expected the operation to fail")
  }
}
