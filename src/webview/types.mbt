///|
/// Events emitted by an Android WebView owned by the Kotlin host. Payloads are
/// UTF-8 normalised before entering MoonBit.
pub(all) enum EventKind {
  Created(String)
  NavigationStarted(String)
  NavigationFinished(String)
  TitleChanged(String)
  /// JavaScript evaluation completion as `(request_id, JSON result)`.
  ScriptResult(String, String)
  Failed(String)
  Destroyed
} derive(Eq, Debug)

///|
/// A WebView callback paired with the caller-provided handle.
pub struct Event {
  handle : Int64
  kind : EventKind
} derive(Eq, Debug)
