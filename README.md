# ajni

`ajni` is a MoonBit-oriented Android JNI runtime and a Kotlin Demo application.
It demonstrates `JavaVM` attach/detach, scoped `JNIEnv` use, local/global
reference ownership, `RegisterNatives`, Java exception checks, standard UTF-8
to Java UTF-16 conversion, lifecycle and `SurfaceHolder` callbacks, and posting
from a native worker thread to Android's UI thread.

## Layout

- `src/`: core MoonBit lifecycle/runtime facade, native FFI declarations, and
  the JNI/ANativeWindow bridge. `src/webview/` is an opt-in WebView package
  with its own MoonBit event contract and native stub.
- `android/host`: reusable `dev.nanaloveyuki.ajni.host` Kotlin bridge expected
  by the JNI library; `android/app` is a demo consumer that exercises the
  lifecycle, Surface, native worker, and UTF-8 string paths.
- `scripts/build-android.ps1`: validates SDK/NDK availability before invoking
  Gradle.

`src/android_runtime` is the MoonBit native host package. Android CMake runs
MoonBit to generate it, compiles the generated source plus MoonBit's runtime
with the NDK, and defines `AJNI_USE_MOONBIT_EXPORTS`. Its stable C export is
`ajni_dispatch_event`. Android builds that opt in to `ajni/webview` also link
`src/webview/ajni_webview_bridge.c` and export
`ajni_dispatch_webview_event`. Java callbacks therefore enter MoonBit without
depending on MoonBit's generated symbol names.

## Build

Host-side MoonBit checks and tests:

```powershell
moon check --target native
moon test --target native -v
```

Install a side-by-side Android NDK in Android Studio's SDK Manager, then build:

```powershell
$env:ANDROID_NDK_HOME = "C:\path\to\android-ndk-r29"
.\scripts\build-android.ps1
```

Import `android/` into Android Studio to create a Gradle wrapper if Gradle is
not already installed. The configured ABIs are `arm64-v8a` and `x86_64`; the
minimum Android API is 24. CMake invokes `moon build` itself, so MoonBit must
be on `PATH` and `MOON_HOME` must point at its installation when non-default.

## Device verification

Start the Pixel AVD or connect a physical Pixel with USB/Wi-Fi debugging, then:

```powershell
adb devices -l
adb install -r android\app\build\outputs\apk\debug\app-debug.apk
adb shell am start -n dev.nanaloveyuki.ajni.demo/.MainActivity
```

The `SurfaceView` changes from blue to teal after its first size callback. Tap
**Start native worker** to create a native-owned pthread, attach it to ART, and
post a callback through `Handler(Looper.getMainLooper())`.

## WebView Host

`Nanaloveyuki/ajni/webview` provides an Android-only host contract for a later
UI abstraction to use. Importing `Nanaloveyuki/ajni` alone does not compile the
WebView facade or its MoonBit native stub. The Android executable must import
the feature package, export `dispatch_from_android` as
`ajni_dispatch_webview_event`, and compile `src/webview/ajni_webview_bridge.c`.
The included demo's `src/android_runtime` and CMake do this already.

The application must attach a caller-owned `FrameLayout` on the Android main
thread before any WebView command:

```kotlin
import dev.nanaloveyuki.ajni.host.NativeBridge

NativeBridge.attachWebViewContainer(container)
```

The feature facade marshals `@webview.create`, `@webview.navigate`,
`@webview.load_html`, `@webview.eval`, `@webview.set_bounds`, and
`@webview.destroy` to that UI thread when called from another thread. A
`WebView` is keyed by an `Int64` handle and is removed and destroyed
automatically when the container detaches.

`@webview.Event` carries the handle plus `EventKind::{Created, NavigationStarted,
NavigationFinished, TitleChanged, ScriptResult, Failed, Destroyed}`. Their
string payloads are converted from Java UTF-16 to UTF-8 in JNI and lossily
decoded at the MoonBit boundary. The host enables JavaScript for `eval`,
disables file/content access and mixed content, and does not register
`addJavascriptInterface`.

## Lifetime rules

- Cache `JavaVM`, never `JNIEnv`; attach only when `GetEnv` reports a detached
  native-created thread and detach only that owned attachment.
- Treat every `JNIEnv` object handle as local until `NewGlobalRef` succeeds;
  global refs are released during shutdown/on-unload while attached to ART.
- Check and clear Java exceptions after calls which can throw before returning
  to MoonBit. Do not continue ordinary JNI work with a pending exception.
- Use `GetStringChars`/`NewString` for Java UTF-16. Do not use `NewStringUTF`
  as a generic UTF-8 conversion API because it accepts modified UTF-8.
