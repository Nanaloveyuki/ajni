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

Initialization publishes feature readiness only after the generic session is
open. JNI adapters reject non-owner managed dispatch before retaining closures
or allocating managed event bytes. Worker threads only attach, post to the
main Looper, and detach; they never enter MoonBit. Queued callbacks carry a
feature generation and are discarded after shutdown or reinitialization.

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
