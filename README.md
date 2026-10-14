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
prevents `void` parameters, enforces 255 array dimensions, 255 explicit parameter
slots, and the classfile's 65,535-byte Modified UTF-8 limit, and keeps raw JNI
pointers out of MoonBit. Instance/interface methods must additionally reserve
one parameter slot for implicit `this`; a descriptor does not prove native ABI.

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

`JniClass::parse` allows at most 65,533 MUTF-8 name bytes, reserving `L` and `;`
so existing non-raising object/descriptor constructors remain valid. Native
method names may use all 65,535 bytes. These are encoding-byte limits, not
UTF-16 lengths or ordinary UTF-8 lengths.

## Android Runtime

Import `Nanaloveyuki/ajni/android` to install an event handler before the Kotlin
host forwards events. Handlers are process-wide, non-thread-safe, and require
main-thread registration/removal/dispatch, or serialized initial bootstrap.

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

Both Android and WebView dispatch capture observer snapshots with no order
guarantee. Removal affects later snapshots; new observers miss the current event;
nested dispatch synchronously takes a fresh snapshot. Direct exports/test emitters
run on the calling thread and must serialize with observer mutation.
Subscriptions/tokens survive shutdown/reinit until explicit removal. Ordinary
shutdown does not clear handlers or installation state, wait for active
calls/handlers, or cancel captured snapshots; admitted callbacks may finish.
JNI unload separately waits for counted activity and releases native references.

Synchronous failures distinguish `NotInitialized`, `RuntimeDestroyed`,
`JavaException`, and `NativeFailure`. Outside Android, readiness is false and
Android/WebView operations raise `NativeFailure` instead of simulated success.
Public `ajni.decode_utf8` is lossy and ignores an initial BOM; WebView event
decoding is strict, preserves BOM, and drops malformed input. Raw JNI preserves
NUL by length, paired surrogates as four-byte UTF-8, and replaces isolated Java
surrogates with U+FFFD. Structured Kotlin fields use the platform UTF-8 encoder
before JNI, with no U+FFFD-byte guarantee for isolated surrogates. MoonBit encoding
requires well-formed UTF-16. See the [wire/text contract](src/webview/README.mbt.md#field-wire-and-text-boundaries)
for canonical fixtures and differing malformed-input tolerance.

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

Initialize and shut down on the main thread. Readiness is published only after
native initialization succeeds. Old runtime/container/view work is discarded;
new views inherit the host's paused state. Shutdown completes pending assets
but does not implicitly detach the caller-owned container.

Use `android/app/src/main/cpp/CMakeLists.txt` as the integration template. It
generates the MoonBit Android host, compiles the MoonBit runtime, and links
`libandroid` and `liblog`. The configured minimum Android API is 24; supported
demo ABIs are `arm64-v8a` and `x86_64`.

## WebView Feature

The WebView host must be attached before `@webview.create`, which loads mutually
exclusive `InitialContent`. Main-thread commands may execute inline, other callers
post to the main Looper. Native acceptance is not execution success; events and
`OperationFailed` may fire before return. Each view has a caller-provided `Int64`
handle.

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

Asset responses accept `100..299` or `400..599`, not redirects. Malformed
responses complete their owned request with an error; wrong handles do not
consume another view's request. Renderer exit destroys the affected view and
completes its pending resources without retry. The trusted message origin is
not a network sandbox for page subresources, redirects, or links.

## Maintenance Scope

Until MoonBit 1.0 is officially released, ajni focuses on fixes to existing
behavior, build correctness, and documentation. Feature expansion is deferred.
See [BUGFIX_PLAN.md](BUGFIX_PLAN.md) for categorized defects, implementation
status, evidence, and remaining validation limits.

## Build And Verify

Current static checks:

```powershell
moon fmt --check
moon check --target native
moon test --target native --build-only
moon info --target native
python3 scripts/check-package-files.py
```

Android/WebView runtime testing is currently deferred. Static checks and APK
compilation do not prove ART, lifecycle, Surface, or WebView behavior. Android
compilation additionally requires the SDK, NDK `29.0.14206865`, CMake, Java 17,
and the pinned Gradle `8.10.2` wrapper under `android/`.

CI verifies the pinned MoonBit version, checks the real frozen publication list,
compiles tests without executing them, and invokes `:host:checkJniContract` before
static lint/APK compilation. The JNI gate pairs actual `javap -private -s` method
blocks with literal C registrations, real native definitions and precise keep
members, then uses real NDK API24 compilers for both ABIs/export configurations.
Its C varargs association/count checks do not prove expression types; manual ABI
review and R8-minified runtime lookup remain separate requirements.

Standalone JNI gate (requires actual compiled Kotlin classes):

```sh
python3 scripts/check-jni-contract.py --class-dir android/host/build/tmp/kotlin-classes/debug \
  --ndk "$ANDROID_NDK_HOME" --moon-home "$MOON_HOME"
```

`.moonignore` independently excludes research `tmp/` and observed Python/build
caches; `.gitignore` is not the publication policy. The gate requires key public,
native, Android-wrapper and checker inputs, and rejects incomplete/diagnostic-only
lists. Local maintenance passed both gates and their negative smoke, native
build-only tests, both-ABI debug APK compilation and host/app lint. A real checked
JVM fixture passed 36 JNI assertions, separately from deferred Android/WebView
runtime validation; see [recorded evidence and deferred scenarios](BUGFIX_PLAN.md#026-维护验收).

The following emulator test command is retained for when runtime testing resumes;
it is not part of the current validation requirement:

```powershell
$env:ANDROID_SDK_ROOT = "C:\path\to\Android\Sdk"
$env:ANDROID_NDK_HOME = "C:\path\to\android-ndk-r29"
$env:ANDROID_SERIAL = "emulator-5554"
.\android\gradlew.bat -p android :app:connectedDebugAndroidTest
```

The GitHub workflow builds the Android demo for both configured ABIs but does
not execute instrumentation tests. Record platform runtime verification as
deferred, not passed, while only static checks or compilation are available.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for feature boundaries, validation, and
pull request requirements.
