///|
#borrow(payload, request_id)
extern "C" fn native_command(
  command : Int32,
  handle : Int64,
  payload : Bytes,
  request_id : Bytes,
) -> Int32 = "ajni_webview_command"

///|
extern "C" fn native_set_bounds(
  handle : Int64,
  x : Int32,
  y : Int32,
  width : Int32,
  height : Int32,
) -> Int32 = "ajni_webview_set_bounds"

///|
extern "C" fn native_last_error() -> Bytes = "ajni_last_error"
