# ajni

`ajni` provides MoonBit-facing JVM JNI primitives and an Android runtime
bridge. The root package builds checked JVM class, type, method, and
native-method declarations; the Android package supplies a stable Kotlin host,
lifecycle and UI-thread callbacks, safe Java UTF-16/MoonBit UTF-8 conversion,
and an optional Android `WebView` feature.

The generic, Android, and WebView packages are separate. Applications that
only need JVM JNI declarations import `Nanaloveyuki/ajni`; Android hosts add
`Nanaloveyuki/ajni/android`; applications that embed a browser additionally
import `Nanaloveyuki/ajni/webview` and link its native stub.

## Install

```powershell
moon add Nanaloveyuki/ajni
```

For the optional WebView feature, import its package from a MoonBit package:

```moonbit nocheck
import {
  "Nanaloveyuki/ajni/webview",
}
```

## Generic JNI Declarations

The root package has no Android dependency. It validates internal class names,
prevents `void` parameters, enforces the JVM's 255 array-dimension and
parameter-slot limits, and keeps raw JNI pointers out of MoonBit:

```moonbit nocheck
let string = try! @ajni.JniClass::parse("java/lang/String")
let signature = try! @ajni.JniMethod::new(
  [@ajni.JniType::object(string)],
  return_type=@ajni.JniType::boolean(),
)
let registration = try! @ajni.NativeMethod::new("nativeAcceptsString", signature)
println(registration.descriptor()) // (Ljava/lang/String;)Z
```

Platform-specific code can consume these values when registering JNI methods.
Raw `JNIEnv*`, `jobject`, global-reference deletion, and Java attachment
remain C-owned because their lifetimes cannot be safely encoded as plain
MoonBit values.

## Android Runtime

Import `Nanaloveyuki/ajni/android` to install an event handler before the
Kotlin host forwards Activity or Surface events. The handler returns a token
that can be removed during shutdown.

```moonbit nocheck
import {
  "Nanaloveyuki/ajni/android",
}

let subscription = @android.install_event_handler(event => match event {
  @android.AndroidEvent::Lifecycle(@android.Lifecycle::Resumed) => println("resumed")
  @android.AndroidEvent::UiTask => println("Android main Looper callback")
  _ => ()
})

// Remove the observer before application shutdown.
@android.remove_event_handler(subscription)
```

`@android.post_to_ui()` schedules an asynchronous callback on Android's main
Looper. `@android.start_worker()` demonstrates a native-owned thread attaching
to ART, posting back to the UI thread, then detaching.

## Android Host

The Android application links the generated MoonBit native artifact and uses
the reusable Kotlin host from the `android:host` Gradle module. Initialize it
once, then forward the Activity lifecycle and attach a caller-owned container
on Android's main thread:

```kotlin
import dev.nanaloveyuki.ajni.host.NativeBridge

NativeBridge.initialize(applicationContext)
NativeBridge.attachWebViewContainer(container)

// Forward every Activity lifecycle state on the main thread.
NativeBridge.lifecycle(state)

// During Activity teardown:
NativeBridge.detachWebViewContainer(container)
NativeBridge.shutdown()
```

`NativeBridge.LIFECYCLE_RESUMED` and `NativeBridge.LIFECYCLE_PAUSED` also
resume and pause attached WebViews. Attaching a replacement container destroys
views in the previous container, so each Activity or host recreation starts
from an explicit new `create` call.

Use `android/app/src/main/cpp/CMakeLists.txt` as the integration template. It
generates the MoonBit Android host, compiles the MoonBit runtime, and links
`libandroid` and `liblog`. The configured minimum Android API is 24; supported
demo ABIs are `arm64-v8a` and `x86_64`.

## WebView Feature

The WebView host must be attached before `@webview.create`. Commands are queued
on Android's main Looper when called from another thread. Each view has a
caller-provided `Int64` handle.

```moonbit nocheck
import {
  "Nanaloveyuki/ajni/webview",
}

let subscription = @webview.install_event_handler(event => match event.kind {
  @webview.EventKind::Created(_) => println("ready")
  @webview.EventKind::ScriptResult(request_id, json) =>
    println("\{request_id}: \{json}")
  @webview.EventKind::PageMessage(body, origin, _is_main_frame) =>
    println("\{origin}: \{body}")
  @webview.EventKind::AssetRequest(request) => println(request.path)
  @webview.EventKind::OperationFailed(operation_id, message) =>
    println("\{operation_id}: \{message}")
  _ => ()
})

try! @webview.create(
  1L,
  "https://app.example.test",
  @webview.InitialContent::Url("https://app.example.test/assets/index.html"),
  document_start_scripts=["globalThis.appReady = true"],
  operation_id="create-browser",
)
try! @webview.eval(1L, "document.title", "title-request")
try! @webview.destroy(1L, operation_id="destroy-browser")
@webview.remove_event_handler(subscription)
```

Importing only the core package does not compile `src/webview/ajni_webview_bridge.c`.
An Android build that imports `ajni/webview` must also include that stub, define
`AJNI_FEATURE_WEBVIEW=1`, and export `ajni_dispatch_webview_event`; the bundled
CMake template demonstrates all three requirements.

The host enables JavaScript for `eval`, disables file and content access,
disables mixed content and multiple windows, enables Safe Browsing on Android
8+, and does not expose `addJavascriptInterface`. Web messages are delivered
only through AndroidX WebKit's trusted-origin listener. Embedded resources use
the same origin's `/assets/` path and emit `AssetRequest`; complete them with
`respond_asset` before the configured timeout.

## Build And Verify

Host-side checks:

```powershell
moon fmt --check
moon check --target native
moon test --target native -v
```

For an Android build, install Android SDK platform tools, NDK `29.0.14206865`,
CMake, Java 17, Gradle, and MoonBit. The bundled demo can run its emulator
tests with:

```powershell
$env:ANDROID_SDK_ROOT = "C:\path\to\Android\Sdk"
$env:ANDROID_NDK_HOME = "C:\path\to\android-ndk-r29"
$env:ANDROID_SERIAL = "emulator-5554"
gradle -p android :app:connectedDebugAndroidTest
```

The GitHub workflow builds the Android demo for both configured ABIs. Device
and emulator validation remain appropriate for changes that alter Android UI
or WebView behavior.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for feature boundaries, validation, and
pull request requirements.
