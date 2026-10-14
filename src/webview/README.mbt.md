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
content is loaded. Commands accept an operation ID and report execution failures
as `EventKind::OperationFailed(operation_id, message)`.

`eval(handle, script, operation_id)` reports its JSON result as
`EventKind::ScriptResult(operation_id, result)`. Page messages include their
origin and main-frame flag. Asset-loader requests emit `AssetRequest` and must
be completed with `respond_asset` before the configured host timeout.

Native command rejection raises the existing `JniError`; acceptance is not
execution success. Main-thread commands may execute inline, others post to the
main Looper. Events, including `OperationFailed`, may fire before command return.
Outside Android, commands raise `NativeFailure` rather than simulated success.

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

## Observers and stopping

Handlers are process-wide and non-thread-safe. Register/remove/dispatch on the
Android main thread, or initially in serialized bootstrap before callbacks.
Direct `dispatch_from_android` calls synchronously on the invoking thread;
callers must serialize it with registration/removal. Snapshot dispatch has no
observer order guarantee: removal affects future snapshots, new observers miss
the current event, and nested dispatch synchronously takes a fresh snapshot.

Subscriptions/tokens survive shutdown/reinit until explicitly removed. Ordinary
shutdown neither clears handlers nor waits for active calls/handlers or cancels
current snapshots; admitted callbacks may finish. JNI unload is a separate
counted-wait/reference-release boundary. Shutdown invalidates queued work and
completes pending assets, but explicit detach/view cleanup remains caller-owned.

## Field wire and text boundaries

The shared canonical domain is empty text for zero fields; otherwise fields are
joined by a single `&`. Names are unique `[a-z_]+`, followed by exactly one `=`.
Values are standard UTF-8 text encoded as URL-safe, unpadded canonical Base64
with zero unused trailing bits. Empty values exist. NUL, BMP and supplementary
scalars are preserved without Unicode normalization; field order follows input,
not a new sorting rule. Binary asset bodies use independent inner Base64, then
encode that ASCII as outer field text, never decode the binary body as UTF-8.

| Field literal | Decoded text |
| --- | --- |
| `body=` | empty |
| `body=AA` | NUL |
| `body=5Lit` | 中 |
| `body=8J-agA` | 🚀 |
| `body=77u_` | U+FEFF (BOM preserved) |
| `body=QVA4` | inner Base64 `AP8`, binary bytes `00 FF` |

The production page-message fixture is
`body=aGVsbG8&origin=aHR0cHM6Ly9vcmJpdC50ZXN0&main_frame=MQ`.
MoonBit also retains legacy `true`/`false` main-frame spellings.

Legacy malformed-input tolerance is not shared: MoonBit fields reject empty
segments, padding, non-alphabet data and invalid UTF-8. Kotlin skips empty
segments, tolerates some noncanonical Base64, and uses charset replacement for
invalid bytes. Only the canonical encoded domain is guaranteed consistent;
private Kotlin parser differences remain source evidence, not a runtime
differential result. No parser, quota fallback, origin/header/status validation,
or visibility has been tightened.

Raw JNI strings preserve embedded NUL by length. Paired Java surrogates become
four-byte UTF-8, isolated surrogates become U+FFFD. Structured Kotlin fields pass
through the platform UTF-8 encoder before JNI; its isolated-surrogate replacement
bytes are not promised to encode U+FFFD. MoonBit encoding requires well-formed
UTF-16. WebView's strict decoder retains an initial BOM and drops malformed-byte
events; public `ajni.decode_utf8` is lossy and ignores an initial BOM by default.
Kotlin public text parameters remain non-null; internal raw echo maps null to
null, while raw WebView callback null becomes empty. No nullable API is added.

The existing Kotlin observer checks the production page-message literal before
JNI, and MoonBit consumes the same literal. Observer success does not prove a
MoonBit handler received it; regressions are compiled only during maintenance.
