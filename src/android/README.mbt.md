# ajni Android Runtime

`Nanaloveyuki/ajni/android` contains the Android-specific JNI host bridge:
lifecycle and Surface events, Android main-Looper callbacks, and a
native-owned worker that attaches to ART. It depends on the generic
`Nanaloveyuki/ajni` descriptor/error package and `Nanaloveyuki/ajni/runtime`.

```mbt check
///|
test {
  let id = @android.install_event_handler(_event => ())
  @android.remove_event_handler(id)
}
```

The Kotlin host invokes managed callbacks only after
`dev.nanaloveyuki.ajni.host.NativeBridge.initialize(...)` on Android's main
Looper. `JNI_OnLoad` only registers natives and the generic Java companion;
MoonBit runtime/package initialization and the process-wide generic session
are established explicitly by `initialize`, using the application's ClassLoader.

`runtime_ready()` is false outside Android. `post_to_ui()` and `start_worker()`
raise the existing `JniError::NativeFailure` for unsupported platforms rather
than reporting a simulated success. On Android, synchronous failures distinguish
`NotInitialized`, `RuntimeDestroyed`, `JavaException`, and `NativeFailure`;
the legacy feature error contains operation diagnostics, not a Throwable's
`toString()` output. The generic runtime retains Java exceptions as handles.
Void event dispatch does not interpret a leftover native status as an uncaught
exception: observers may catch generic Java errors and return normally. Exports
that return errors must convert them explicitly, preserving the original Java
Throwable rather than reconstructing it from diagnostic text.

Executable hosts import this library rather than `Nanaloveyuki/ajni/android_runtime`.
Their final-link wrappers must export `dispatch_from_android` as
`ajni_dispatch_event`, `create_int_callback` as `ajni_create_int_callback`, and
`handled_observer_error` as `ajni_handled_observer_error`; optional WebView hosts
also export `@webview.dispatch_from_android` as `ajni_dispatch_webview_event`.
Dependency export lists are not inherited. The `android_runtime` executable
provides the bundled host wrappers; the two callback helpers share its generic
callback/session implementation and preserve Java errors through the JNI bridge.
Call them only after host initialization on the runtime owner thread.

The bundled Context acceptance fixture additionally defines
`AJNI_CONTEXT_TEST_EXPORTS` and exports `application_context_for_test` as
`ajni_application_context_for_test` and `context_package_name_for_test` as
`ajni_context_package_name_for_test`. These staged instrumentation helpers are
optional for custom executable hosts and are not application acquisition APIs.

Initialization publishes feature readiness only after the generic session is
open. JNI adapters reject non-owner managed dispatch before retaining closures
or allocating managed event bytes. Worker threads only attach, post to the
main Looper, and detach; they never enter MoonBit. Queued callbacks carry a
feature generation and are discarded after shutdown or reinitialization.

## Application Context

Starting with ajni 0.3.6, `application_context() -> @runtime.JavaObject raise
@runtime.RuntimeError` acquires the application Context supplied explicitly by
`NativeBridge.initialize(context)`. The host normalizes Activity or wrapper input
with `getApplicationContext()`, rejects null and conflicting active-host identities,
and publishes the Context only after native initialization succeeds.

Call on Android's main owner thread after initialization; package initialization
is too early and raises `NotInitialized`. Each acquisition owns a separate global
reference to the same Java object. Close it explicitly when finished. Closing one
acquisition leaves other acquisitions usable; aliases of one handle share its
close state. No Activity is retained by this interface.

Feature shutdown clears the host's Context and new acquisition raises
`JavaThrown` with the getter's original `IllegalStateException`. Existing Context
handles and the generic session survive feature shutdown, and reinitialization
enables acquisition again. Explicit generic `Session.close()` invalidates all old
handles; reopening never rebinds them. Reinitialize the host before acquiring
after a session close. Java lookup/getter exceptions retain their original
Throwable. Non-native targets and native desktop JVM hosts raise
`UnsupportedTarget` before resolving any Android classes.

This is synchronous object access on the main thread. It does not provide an
off-main asset-reading facility or permission Activity/result delivery. The
existing native worker only posts back to the owner; never move JavaObject
handles or MoonBit closures onto it for blocking I/O.

## Observers and stopping

Handlers are process-wide and non-thread-safe. On Android, register, remove,
and dispatch on the main thread. Direct MoonBit calls must also respect the
runtime owner contract; entering MoonBit on another thread is not made safe by
an eventual native check. `emit_for_test` is synchronous.

Each dispatch captures a snapshot, without observer ordering guarantees.
Removal affects future dispatch only; a newly installed handler does not receive
the current event. Nested dispatch runs synchronously with a fresh snapshot.

Subscriptions and tokens survive shutdown/reinitialization until explicit
removal. Ordinary shutdown does not clear handlers or callback-installation
state, wait for active calls/handlers, or cancel a captured snapshot; admitted
callbacks may finish. The generic session also survives feature shutdown;
explicit generic `Session.close()` remains an owner-only, non-active operation.
Reinitializing the feature opens a new generic session if the former was closed.
JNI unload waits for native workers/leases and releases only native feature
metadata, never managed subscriptions on the unknown unload thread. Live
session/class references intentionally prevent arbitrary ClassLoader unloading.
`post_to_ui()` remains asynchronous, including on the main thread.
