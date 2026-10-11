# ajni WebView

`Nanaloveyuki/ajni/webview` is the optional Android browser feature. Its native
stub and Kotlin host are not part of the ajni core-only build. It uses the
host's trusted-origin WebMessage channel and asset-loader callbacks; it never
installs an unrestricted JavaScript interface.

```mbt check
///|
test {
  let id = @webview.install_event_handler(_event => ())
  @webview.remove_event_handler(id)
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

Native command rejection raises the existing `JniError`; asynchronous host
execution failures remain `OperationFailed` events. Outside Android, commands
raise `NativeFailure` rather than reporting a simulated success.

Queued commands and callbacks retain runtime, container, and view identities.
Replacing a container or destroying a view invalidates its old work, including
late script results. Newly created views inherit the host's paused state.
Shutdown invalidates work and completes outstanding assets; the caller still
owns explicit container detachment and view cleanup.

Asset responses accept only HTTP status `100..299` or `400..599`, matching
`WebResourceResponse`. Malformed responses terminate their own pending request;
a wrong handle does not consume another view's request. Only an actual timeout
returns 504; invalid responses, interruption, and internal failures are distinct.
Renderer exit cleans up the failed view and pending assets without retrying.

The trusted origin restricts the message channel and explicit asset navigation.
It is not a general network sandbox for page subresources, links, or redirects.
