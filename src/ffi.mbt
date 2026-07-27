///|
/// Installs the native-to-MoonBit event trampoline. `callback` is owned by C
/// until it is replaced or the runtime shuts down.
#borrow(call_closure)
#owned(callback)
extern "C" fn native_install_event_callback(
  call_closure : FuncRef[((Int, Int, Int) -> Unit, Int, Int, Int) -> Unit],
  callback : (Int, Int, Int) -> Unit,
) -> Unit = "ajni_install_event_callback"

///|
extern "C" fn native_runtime_ready() -> Int = "ajni_runtime_ready"

///|
extern "C" fn native_post_ui_callback() -> Int = "ajni_post_ui_callback"

///|
extern "C" fn native_start_worker() -> Int = "ajni_start_worker"

///|
extern "C" fn native_emit_for_test(
  kind : Int,
  first : Int,
  second : Int,
) -> Unit = "ajni_emit_for_test"

///|
#borrow(payload)
extern "C" fn native_webview_command(
  command : Int,
  handle : Int64,
  payload : Bytes,
) -> Int = "ajni_webview_command"

///|
extern "C" fn native_webview_set_bounds(
  handle : Int64,
  x : Int,
  y : Int,
  width : Int,
  height : Int,
) -> Int = "ajni_webview_set_bounds"
