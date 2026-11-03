///|
/// Stable Android C entry point. The JNI bridge links this package in a
/// production Android build and calls the exported `ajni_dispatch_event`.
pub fn dispatch(kind : Int32, first : Int32, second : Int32) -> Unit {
  @android.dispatch_from_android(kind, first, second)
}

///|
pub fn dispatch_webview(
  kind : Int32,
  handle : Int64,
  payload : Bytes,
  detail : Bytes,
) -> Unit {
  @webview.dispatch_from_android(kind, handle, payload, detail)
}

///|
pub fn create_int_callback(env : UInt64) -> UInt64 {
  @android.create_int_callback(env)
}

///|
pub fn handled_observer_error() -> Int32 {
  @android.handled_observer_error()
}

///|
fn main {
  ()
}
