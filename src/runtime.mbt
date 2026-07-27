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
/// Stable C export used by the Android JNI bridge. Applications embedding the
/// MoonBit native artifact link this symbol as `ajni_dispatch_event`; callers
/// must still install their own event handler to observe the event payload.
pub fn dispatch_from_android(kind : Int, first : Int, second : Int) -> Unit {
  dispatch_native_event(kind, first, second)
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
