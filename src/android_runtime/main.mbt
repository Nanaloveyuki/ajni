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
let context_before_init_rejected : Bool = try {
  let context = @android.application_context()
  context.close() catch {
    _ => ()
  }
  false
} catch {
  @runtime.NotInitialized => true
  _ => false
}

///|
pub fn application_context_for_test(env : UInt64, stage : Int32) -> UInt64 {
  @android.application_context_for_test(
    env, stage, context_before_init_rejected,
  )
}

///|
pub fn context_package_name_for_test(env : UInt64) -> UInt64 {
  @android.context_package_name_for_test(env)
}

///|
fn main {
  ()
}
