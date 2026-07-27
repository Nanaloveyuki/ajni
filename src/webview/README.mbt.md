# ajni WebView

`Nanaloveyuki/ajni/webview` is the optional Android browser feature. Its native
stub and Kotlin host are not part of the ajni core-only build. It uses the
host's trusted-origin WebMessage channel and asset-loader callbacks; it never
installs an unrestricted JavaScript interface.

```mbt check
///|
test {
  let id = install_event_handler(_event => ())
  remove_event_handler(id)
}
```

The Kotlin host must attach a caller-owned `FrameLayout` before creating a
view. `create` requires one HTTPS trusted origin and mutually exclusive
`InitialContent`; document-start scripts are registered before that initial
content is loaded. All asynchronous commands accept an operation ID and report
failures as `EventKind::OperationFailed(operation_id, message)`.

`eval(handle, script, operation_id)` reports its JSON result as
`EventKind::ScriptResult(operation_id, result)`. Page messages include their
origin and main-frame flag. Asset-loader requests emit `AssetRequest` and must
be completed with `respond_asset` before the configured host timeout.
