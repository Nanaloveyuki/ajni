///|
#borrow(payload, request_id)
extern "C" fn native_command(
  command : Int,
  handle : Int64,
  payload : Bytes,
  request_id : Bytes,
) -> Int = "ajni_webview_command"

///|
extern "C" fn native_set_bounds(
  handle : Int64,
  x : Int,
  y : Int,
  width : Int,
  height : Int,
) -> Int = "ajni_webview_set_bounds"
