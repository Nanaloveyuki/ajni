///|
/// Each installation transfers one callback reference to C; replacement releases
/// the old reference. Feature shutdown keeps process subscriptions intact.
#borrow(call_closure)
#owned(callback)
extern "C" fn native_install_event_callback(
  call_closure : FuncRef[
    ((Int32, Int32, Int32) -> Unit, Int32, Int32, Int32) -> Unit,
  ],
  callback : (Int32, Int32, Int32) -> Unit,
) -> Unit = "ajni_install_event_callback"

///|
extern "C" fn native_runtime_ready() -> Int32 = "ajni_runtime_ready"

///|
extern "C" fn native_post_ui_callback() -> Int32 = "ajni_post_ui_callback"

///|
extern "C" fn native_start_worker() -> Int32 = "ajni_start_worker"

///|
extern "C" fn native_last_error() -> Bytes = "ajni_last_error"

///|
extern "C" fn native_emit_for_test(
  kind : Int32,
  first : Int32,
  second : Int32,
) -> Unit = "ajni_emit_for_test"
