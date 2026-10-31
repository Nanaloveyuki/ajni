///|
let handlers : Map[Int32, (AndroidEvent) -> Unit] = Map([])

///|
let callback_installation : Map[Int32, Unit] = Map([])

///|
let next_handler_id : Ref[Int32] = Ref(1)

///|
fn ensure_callback_installed() -> Unit {
  if !callback_installation.contains(0) {
    let callback = fn(kind : Int32, first : Int32, second : Int32) {
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
/// Registers a process-wide, non-thread-safe observer. Use the runtime owner
/// thread; subscriptions survive Android feature shutdown/reinitialization.
pub fn install_event_handler(handler : (AndroidEvent) -> Unit) -> Int32 {
  ensure_callback_installed()
  let id = next_handler_id.val
  next_handler_id.val = id + 1
  handlers.set(id, handler)
  id
}

///|
/// Removes the token from future snapshots, not an already captured snapshot.
pub fn remove_event_handler(id : Int32) -> Unit {
  ignore(handlers.remove(id))
}

///|
/// Requests a callback on Android's main Looper. The callback is asynchronous
/// even if the caller is already on the UI thread.
pub fn post_to_ui() -> Unit raise @ajni.JniError {
  check_native_status(native_post_ui_callback())
}

///|
/// Starts a native-owned worker thread. The bridge attaches it to the VM,
/// then posts `WorkerAttached` and `UiTask` to the Android main Looper before
/// detaching it.
pub fn start_worker() -> Unit raise @ajni.JniError {
  check_native_status(native_start_worker())
}

///|
fn check_native_status(status : Int32) -> Unit raise @ajni.JniError {
  match status {
    1 => ()
    0 => raise NotInitialized
    -1 => raise RuntimeDestroyed
    -3 => raise JavaException(@ajni.decode_utf8(native_last_error()))
    _ => raise NativeFailure(@ajni.decode_utf8(native_last_error()))
  }
}

///|
pub fn runtime_ready() -> Bool {
  native_runtime_ready() != 0
}

///|
/// Synchronously emits on the calling owner thread.
pub fn emit_for_test(kind : Int32, first : Int32, second : Int32) -> Unit {
  native_emit_for_test(kind, first, second)
}

///|
/// Synchronous export linked as `ajni_dispatch_event`; callers must be on the
/// runtime owner. Snapshots have no order guarantee; nested dispatch is fresh.
pub fn dispatch_from_android(
  kind : Int32,
  first : Int32,
  second : Int32,
) -> Unit {
  dispatch_native_event(kind, first, second)
}

///|
fn dispatch_native_event(kind : Int32, first : Int32, second : Int32) -> Unit {
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
  let snapshot : Array[(AndroidEvent) -> Unit] = []
  for _, handler in handlers {
    snapshot.push(handler)
  }
  for handler in snapshot {
    handler(event)
  }
}
