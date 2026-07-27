///|
let handlers : Map[Int, (AndroidEvent) -> Unit] = Map([])

///|
let callback_installation : Map[Int, Unit] = Map([])

///|
let next_handler_id : Ref[Int] = Ref(1)

///|
fn ensure_callback_installed() -> Unit {
  if !callback_installation.contains(0) {
    let callback = fn(kind : Int, first : Int, second : Int) {
      dispatch_native_event(kind, first, second)
    }
    native_install_event_callback(
      fn(callback, kind, first, second) { callback(kind, first, second) },
      callback,
    )
    callback_installation.set(0, ())
  }
}

///|
/// Registers a process-wide Android event observer and returns its removal
/// token. Register before Kotlin calls `NativeBridge.initialize`.
pub fn install_event_handler(handler : (AndroidEvent) -> Unit) -> Int {
  ensure_callback_installed()
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
/// Requests a callback on Android's main Looper. The callback is asynchronous
/// even if the caller is already on the UI thread.
pub fn post_to_ui() -> Unit raise JniError {
  if native_post_ui_callback() == 0 {
    raise NotInitialized
  }
}

///|
/// Starts a native-owned worker thread. The bridge attaches it to the VM,
/// reports `WorkerAttached`, posts to the UI thread, then detaches it.
pub fn start_worker() -> Unit raise JniError {
  if native_start_worker() == 0 {
    raise NotInitialized
  }
}

///|
pub fn runtime_ready() -> Bool {
  native_runtime_ready() != 0
}

///|
/// Emits a bridge event for host-side tests and integration harnesses.
pub fn emit_for_test(kind : Int, first : Int, second : Int) -> Unit {
  native_emit_for_test(kind, first, second)
}

///|
/// Creates a WebView in the Kotlin host's attached `FrameLayout` and starts
/// navigating to `url`. The command is always queued on Android's main Looper.
pub fn webview_create(handle : Int64, url : String) -> Unit raise JniError {
  webview_command(1, handle, url)
}

///|
pub fn webview_navigate(handle : Int64, url : String) -> Unit raise JniError {
  webview_command(2, handle, url)
}

///|
pub fn webview_load_html(handle : Int64, html : String) -> Unit raise JniError {
  webview_command(3, handle, html)
}

///|
pub fn webview_eval(handle : Int64, script : String) -> Unit raise JniError {
  webview_command(4, handle, script)
}

///|
pub fn webview_destroy(handle : Int64) -> Unit raise JniError {
  webview_command(5, handle, "")
}

///|
pub fn webview_set_bounds(
  handle : Int64,
  x : Int,
  y : Int,
  width : Int,
  height : Int,
) -> Unit raise JniError {
  if native_webview_set_bounds(handle, x, y, width, height) == 0 {
    raise NotInitialized
  }
}

///|
fn webview_command(
  command : Int,
  handle : Int64,
  payload : String,
) -> Unit raise JniError {
  if native_webview_command(command, handle, encode_utf8(payload)) == 0 {
    raise NotInitialized
  }
}

///|
/// Stable C export used by the Android JNI bridge. Applications embedding the
/// MoonBit native artifact link this symbol as `ajni_dispatch_event`; callers
/// must still install their own event handler to observe the event payload.
pub fn dispatch_from_android(kind : Int, first : Int, second : Int) -> Unit {
  dispatch_native_event(kind, first, second)
}

///|
/// Stable C export for UTF-8 WebView callbacks. The native bridge invokes this
/// only after the Kotlin host has returned to its Android UI thread.
pub fn dispatch_webview_from_android(
  kind : Int,
  handle : Int64,
  payload : Bytes,
) -> Unit {
  let event = match kind {
    30 => WebViewEvent::Created(decode_utf8(payload))
    31 => NavigationStarted(decode_utf8(payload))
    32 => NavigationFinished(decode_utf8(payload))
    33 => TitleChanged(decode_utf8(payload))
    34 => ScriptResult(decode_utf8(payload))
    35 => Failed(decode_utf8(payload))
    36 => Destroyed
    _ => return
  }
  for _, handler in handlers {
    handler(WebView(handle, event))
  }
}

///|
fn dispatch_native_event(kind : Int, first : Int, second : Int) -> Unit {
  let event = match kind {
    1 => Lifecycle(Created)
    2 => Lifecycle(Started)
    3 => Lifecycle(Resumed)
    4 => Lifecycle(Paused)
    5 => Lifecycle(Stopped)
    6 => Lifecycle(Destroyed)
    10 => SurfaceCreated(first, second)
    11 => SurfaceChanged(first, second)
    12 => SurfaceDestroyed
    20 => WorkerAttached
    21 => UiTask
    _ => return
  }
  for _, handler in handlers {
    handler(event)
  }
}
