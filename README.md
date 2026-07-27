# ajni

`ajni` provides MoonBit-facing Android JNI runtime primitives. It supplies a
stable Kotlin host, lifecycle and UI-thread callbacks, safe Java UTF-16/MoonBit
UTF-8 conversion, and an optional Android `WebView` feature.

The core package and WebView feature are separate. Applications that only need
lifecycle or UI callbacks import `Nanaloveyuki/ajni`; applications that embed a
browser additionally import `Nanaloveyuki/ajni/webview` and link its native
stub.

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

## Core Runtime

Install an event handler before the Kotlin host forwards Activity or Surface
events. The handler returns a token that can be removed during shutdown.

```moonbit nocheck
import {
  "Nanaloveyuki/ajni",
}

let subscription = @ajni.install_event_handler(event => match event {
  @ajni.AndroidEvent::Lifecycle(@ajni.Lifecycle::Resumed) => println("resumed")
  @ajni.AndroidEvent::UiTask => println("Android main Looper callback")
  _ => ()
})

// Remove the observer before application shutdown.
@ajni.remove_event_handler(subscription)
```

`@ajni.post_to_ui()` schedules an asynchronous callback on Android's main
Looper. `@ajni.start_worker()` demonstrates a native-owned thread attaching to
ART, posting back to the UI thread, then detaching.

## Android Host

The Android application links the generated MoonBit native artifact and uses
the reusable Kotlin host from the `android:host` Gradle module. Initialize it
once, then forward the Activity lifecycle and attach a caller-owned container
on Android's main thread:

```kotlin
import dev.nanaloveyuki.ajni.host.NativeBridge

NativeBridge.initialize(applicationContext)
NativeBridge.attachWebViewContainer(container)

// During Activity teardown:
NativeBridge.detachWebViewContainer(container)
NativeBridge.shutdown()
```

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
  @webview.EventKind::Failed(message) => println(message)
  _ => ()
})

try! @webview.create(1L, "https://example.com")
try! @webview.eval(1L, "document.title", "title-request")
try! @webview.destroy(1L)
@webview.remove_event_handler(subscription)
```

Importing only the core package does not compile `src/webview/ajni_webview_bridge.c`.
An Android build that imports `ajni/webview` must also include that stub, define
`AJNI_FEATURE_WEBVIEW=1`, and export `ajni_dispatch_webview_event`; the bundled
CMake template demonstrates all three requirements.

The host enables JavaScript for `eval`, disables file and content access,
disables mixed content and multiple windows, enables Safe Browsing on Android
8+, and does not expose `addJavascriptInterface`.

## Build And Verify

Host-side checks:

```powershell
moon fmt --check
moon check --target native
moon test --target native -v
```

For an Android build, install Android SDK platform tools, NDK `29.0.14206865`,
CMake, Java 17, Gradle 8.10.2, and MoonBit. Then run:

```powershell
$env:ANDROID_NDK_HOME = "C:\path\to\android-ndk-r29"
.\scripts\build-android.ps1
```

The GitHub workflow builds the Android demo for both configured ABIs. Device
and emulator validation remain appropriate for changes that alter Android UI
or WebView behavior.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for feature boundaries, validation, and
pull request requirements.
