///|
/// Stable Android C entry point. The JNI bridge links this package in a
/// production Android build and calls the exported `ajni_dispatch_event`.
pub fn dispatch(kind : Int, first : Int, second : Int) -> Unit {
  @ajni.dispatch_from_android(kind, first, second)
}

///|
pub fn dispatch_webview(
  kind : Int,
  handle : Int64,
  payload : Bytes,
  detail : Bytes,
) -> Unit {
  @webview.dispatch_from_android(kind, handle, payload, detail)
}

///|
fn main {
  ()
}
