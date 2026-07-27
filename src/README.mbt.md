# ajni Core

`Nanaloveyuki/ajni` is the Android JNI runtime package. It exposes lifecycle,
surface, worker, and Android main-Looper callbacks without importing the
optional WebView bridge.

## Runtime Events

```mbt check
///|
test {
  let id = install_event_handler(_event => ())
  remove_event_handler(id)
}
```

The Kotlin host invokes native callbacks only after
`dev.nanaloveyuki.ajni.host.NativeBridge.initialize(...)`. Use
`post_to_ui()` for an asynchronous main-Looper callback and `start_worker()`
for a native-owned thread that attaches to ART.

## Optional Browser Support

Browser support is deliberately outside this package. Import
`Nanaloveyuki/ajni/webview` only when the Android application embeds a
`WebView`; its package documentation describes the command and event contract.
