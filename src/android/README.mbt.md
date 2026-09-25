# ajni Android Runtime

`Nanaloveyuki/ajni/android` contains the Android-specific JNI host bridge:
lifecycle and Surface events, Android main-Looper callbacks, and a
native-owned worker that attaches to ART. It depends on the generic
`Nanaloveyuki/ajni` descriptor and error package.

```mbt check
///|
test {
  let id = @android.install_event_handler(_event => ())
  @android.remove_event_handler(id)
}
```

The Kotlin host invokes callbacks only after
`dev.nanaloveyuki.ajni.host.NativeBridge.initialize(...)`. This package is
not required for JVM hosts that only use generic JNI declaration types.
