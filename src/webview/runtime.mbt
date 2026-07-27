///|
let handlers : Map[Int, (Event) -> Unit] = Map([])

///|
let next_handler_id : Ref[Int] = Ref(1)

///|
/// Registers a process-wide WebView event observer and returns its removal
/// token. Register before the Kotlin host emits callbacks.
pub fn install_event_handler(handler : (Event) -> Unit) -> Int {
  let id = next_handler_id.val
  next_handler_id.val = id + 1
  handlers.set(id, handler)
  id
}

///|
pub fn remove_event_handler(id : Int) -> Unit {
  ignore(handlers.remove(id))
}

///|
/// Creates a WebView in the Kotlin host's attached `FrameLayout` and starts
/// navigating to `url`. The command is always queued on Android's main Looper.
pub fn create(handle : Int64, url : String) -> Unit raise @ajni.JniError {
  command(1, handle, url, "")
}

///|
pub fn navigate(handle : Int64, url : String) -> Unit raise @ajni.JniError {
  command(2, handle, url, "")
}

///|
pub fn load_html(handle : Int64, html : String) -> Unit raise @ajni.JniError {
  command(3, handle, html, "")
}

///|
pub fn eval(
  handle : Int64,
  script : String,
  request_id : String,
) -> Unit raise @ajni.JniError {
  command(4, handle, script, request_id)
}

///|
pub fn destroy(handle : Int64) -> Unit raise @ajni.JniError {
  command(5, handle, "", "")
}

///|
pub fn set_bounds(
  handle : Int64,
  x : Int,
  y : Int,
  width : Int,
  height : Int,
) -> Unit raise @ajni.JniError {
  if native_set_bounds(handle, x, y, width, height) == 0 {
    raise NotInitialized
  }
}

///|
fn command(
  command : Int,
  handle : Int64,
  payload : String,
  request_id : String,
) -> Unit raise @ajni.JniError {
  if native_command(
      command,
      handle,
      @ajni.encode_utf8(payload),
      @ajni.encode_utf8(request_id),
    ) ==
    0 {
    raise NotInitialized
  }
}

///|
/// Stable C export for UTF-8 callbacks from the optional Android WebView stub.
pub fn dispatch_from_android(
  kind : Int,
  handle : Int64,
  payload : Bytes,
  detail : Bytes,
) -> Unit {
  let event_kind = match kind {
    30 => EventKind::Created(@ajni.decode_utf8(payload))
    31 => NavigationStarted(@ajni.decode_utf8(payload))
    32 => NavigationFinished(@ajni.decode_utf8(payload))
    33 => TitleChanged(@ajni.decode_utf8(payload))
    34 => ScriptResult(@ajni.decode_utf8(detail), @ajni.decode_utf8(payload))
    35 => Failed(@ajni.decode_utf8(payload))
    36 => Destroyed
    _ => return
  }
  let event = { handle, kind: event_kind }
  for _, handler in handlers {
    handler(event)
  }
}
