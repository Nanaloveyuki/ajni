# ajni WebView

`Nanaloveyuki/ajni/webview` is the optional Android browser feature. Its native
stub and Kotlin host are not part of the ajni core-only build.

```mbt check
///|
test {
  let id = install_event_handler(_event => ())
  remove_event_handler(id)
}
```

The Kotlin host must attach a caller-owned `FrameLayout` before creating a
view. `eval(handle, script, request_id)` reports its JSON result as
`EventKind::ScriptResult(request_id, result)`.
