# ajni

`ajni` provides checked bidirectional Java↔MoonBit JNI calls and an Android
host adapter. The root package validates JVM declarations without JNI headers;
`runtime` owns sessions, global-reference handles, typed calls and callbacks.
Android supplies lifecycle, Surface and main-Looper integration. WebView is an
optional consumer, not a dependency of the generic runtime.

| Package | Purpose |
| --- | --- |
| `Nanaloveyuki/ajni` | Checked class, type, method and native-method declarations |
| `Nanaloveyuki/ajni/runtime` | Sessions, objects, calls, fields, arrays, exceptions and callbacks |
| `Nanaloveyuki/ajni/unsafe` | Explicit raw JNI host/ref/function-pointer operations |
| `Nanaloveyuki/ajni/invocation` | Optional desktop JVM creation using `dynlib` |
| `Nanaloveyuki/ajni/android` | Android host adapter |
| `Nanaloveyuki/ajni/webview` | Optional Android WebView integration |

See [ADR 0001](docs/adr/0001-general-jni.md) for ownership, threading and
acceptance contracts. The general runtime APIs are available starting with ajni 0.3.0.
The pinned toolchain currently requires `0.x.y` package versions.

HarmonyOS/OpenHarmony exploration stays in the unpublished `tests/ohos/` NAPI
fixture, separate from JNI. The API26 x86_64 DevEco emulator passed the native
suite and Worker-termination cleanup; arm64 is build-only, and independent
OpenHarmony runtime support is unverified. See [ADR 0002](docs/adr/0002-harmony-validation.md)
for reproduction commands and limits.

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

## Bidirectional Runtime

The host initializes the matching MoonBit runtime and package globals on one OS
thread, then calls `ajni_rt_host_open(env, application_loader)` from
[`ajni_runtime.h`](src/runtime/ajni_runtime.h). Every Java-to-MoonBit entry must
call `ajni_rt_host_enter` **before** touching managed values, and balance it with
`ajni_rt_host_leave`. `JNI_OnLoad` registers natives but does not choose the owner.
The runnable host example is [`tests/jvm/bridge.c`](tests/jvm/bridge.c).

```moonbit nocheck
let session = @runtime.Session::current()
let builder = session.find_class(@ajni.JniClass::parse("java/lang/StringBuilder"))
let instance = builder.lookup_constructor([]).new([])
let append = builder.instance_method("append", @ajni.JniMethod::new(
  [@ajni.JniType::object(@ajni.JniClass::parse("java/lang/String"))],
  return_type=@ajni.JniType::object(@ajni.JniClass::parse("java/lang/StringBuilder")),
))
ignore(append.call([@runtime.Object(Some(session.new_string("MoonBit")))], receiver=instance))
let callback = session.callback(
  @ajni.JniMethod::new([@ajni.JniType::int()], return_type=@ajni.JniType::int()),
  (args) => match args[0] {
    Int(value) => @runtime.Int(value + 1)
    _ => raise @runtime.InvalidArgument("Expected int")
  },
)
// Pass callback.object() to Java; AjniCallback.invoke(41) returns Integer(42).
```

Compile the Java companion under `java/src/main/java` with the host application.
Its fixed dispatcher boxes primitive results and checks argument wrappers against
the registered descriptor. Arbitrary Java interfaces require a Java/Kotlin adapter.
Same-owner synchronous reentry is supported; foreign-thread invocation is rejected
before MoonBit entry. Java may close a callback from any thread; owner entry drains
the deferred closure release. Close callbacks when no longer needed.

Public objects retain global references. Aliases share close state; session close
invalidates all descendants and is rejected with `Busy` during active calls.
`JavaThrown(object)` retains the original Throwable and can be caught or returned
through a callback without replacing its identity. Strings preserve UTF-16 code
units, including NUL and unpaired surrogates. Arrays copy regions, never pin Java
memory across a callback. See the [runtime API guide](src/runtime/README.mbt.md).

For a native executable, `@invocation.create(full_library_path, options)` loads
`JNI_CreateJavaVM` through `Nanaloveyuki/dynlib` without prelinking `libjvm`.
Pass the companion/application classpath explicitly. Creation is attempted once;
after that attempt its library stays loaded until process exit. `Session.close`
does not call `DestroyJavaVM`. Loader/creation failures use `InvocationError`;
host initialization failures use `RuntimeError`. See
[`tests/invocation`](tests/invocation) for a complete executable consumer.

JNI headers must be supplied when compiling native stubs: JDK headers for desktop,
NDK headers for Android. Without headers, operations report `UnsupportedTarget`.
Only Linux/JDK and configured Android ABIs have execution/build evidence here;
Windows/macOS desktop ABI support is not claimed.

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

Existing behavior continues to receive bug fixes, build corrections, and
documentation updates. General bidirectional JNI runtime work follows
[ADR 0001](docs/adr/0001-general-jni.md); WebView remains an optional consumer
and does not expand as part of the core closure. ADR 0001 records the JNI and
Android evidence with its remaining limits, ADR 0002 records the Harmony
fixture boundary, and `CHANGELOG.md` carries the historical fix evidence.

## Build And Verify

Current static checks:

```powershell
moon fmt --check
moon check --target native
moon test --target native --build-only
moon info --target native
python3 scripts/check-package-files.py
```

Real JVM smoke on Linux requires JDK 21, a C compiler and the pinned MoonBit
toolchain. It builds an isolated workspace, executes Java with `-Xcheck:jni`,
then executes a native-hosted JVM without prelinking `libjvm`:

```sh
moon update
python3 scripts/run-jvm-smoke.py
```

Both host modes passed locally, including negative loader/creation cases. CI runs
this command alongside native check/build-only unit tests and publication checks.
Android compilation requires SDK35, NDK `29.0.14206865`, CMake `4.1.2`, Java 17
and the pinned Gradle `8.10.2` wrapper. The JNI gate pairs actual bytecode with
C registrations/definitions and precise bridge/companion keep rules, then uses
real NDK API24 compilers for both ABIs/export configurations. Its varargs checks
prove association/count, not expression types or R8-minified runtime lookup.

Standalone JNI gate (requires actual compiled Kotlin classes):

```sh
python3 scripts/check-jni-contract.py --class-dir android/host/build/tmp/kotlin-classes/debug \
  --ndk "$ANDROID_NDK_HOME" --moon-home "$MOON_HOME"
```

`.moonignore` independently excludes research `tmp/` and observed Python/build
caches; `.gitignore` is not the publication policy. The gate requires key public,
native, Android-wrapper and checker inputs, and rejects incomplete/diagnostic-only
lists. Local validation passed native check/build-only tests, checked-JVM fixtures,
both-ABI debug APK/instrumentation compilation, the JNI contract and host/app lint.
API37 x86_64 emulator execution passed all 8 instrumentation tests: captured
callbacks, foreign rejection/close, handled observer errors, Activity recreation,
feature shutdown/reinit, Unicode and WebView message/resource/container paths.
The demo also launched and rendered its native Surface. arm64 device execution,
API24 runtime and R8-minified lookup remain unverified. See
[ADR evidence](docs/adr/0001-general-jni.md#设计依据与已知证据).

With a connected emulator, execute the Android behavior tests separately:

```powershell
$env:ANDROID_SDK_ROOT = "C:\path\to\Android\Sdk"
$env:ANDROID_NDK_HOME = "C:\path\to\android-ndk-r29"
$env:ANDROID_SERIAL = "emulator-5554"
.\android\gradlew.bat -p android :app:connectedDebugAndroidTest
```

The GitHub workflow builds both configured ABIs and packages instrumentation tests
without executing them. Record platform runtime verification as deferred, not
passed, while only static checks or compilation are available.

## CI And Releases

`Build` runs on `main` pushes, pull requests to `main`, and manual dispatch.
Independent MoonBit and Android jobs feed the required `ci` gate. Each successful
run retains the MoonBit source ZIP and Android debug APK/instrumentation APK/host
AAR for 30 days; API documentation data and Android lint reports for 14 days.
API data comes from `moon doc`, not a deployed documentation website.

Pushing a new stable `vX.Y.Z` tag triggers `Release`. The tag must match
`moon.mod`, have a nonempty matching changelog section, and point to a commit in
`main` history. Release reuses all Build checks, then creates a GitHub Release
with source/Android ZIPs and `SHA256SUMS`. Existing releases are never overwritten.
Android assets are debug integration builds, not production-signed applications.
See [CONTRIBUTING.md](CONTRIBUTING.md#release-process) for maintainer commands.

Optional Mooncakes delivery publishes the validated source artifact after the
GitHub Release. Enable `MOONCAKES_PUBLISH=true` and configure the
`MOONCAKES_CREDENTIALS` Actions secret as described in
[the registry setup instructions](CONTRIBUTING.md#optional-mooncakes-publishing).
Without that configuration, GitHub Releases remain usable and registry delivery
is skipped.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for feature boundaries, validation, and
pull request requirements.
