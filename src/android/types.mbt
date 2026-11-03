///|
/// Android lifecycle values forwarded by the Kotlin bridge.
pub(all) enum Lifecycle {
  Created
  Started
  Resumed
  Paused
  Stopped
  Destroyed
} derive(Eq, Debug)

///|
#deprecated("Use `==` instead", skip_current_package=true)
#doc(hidden)
pub extend Lifecycle with Eq::{equal, not_equal}

///|
#deprecated("Use `Debug::to_repr` instead", skip_current_package=true)
#doc(hidden)
pub extend Lifecycle with Debug::{to_repr}

///|
/// Events delivered from the Android bridge. Every event handler runs on the
/// Android main Looper; worker-originated events are first posted there by the
/// JNI bridge.
pub(all) enum AndroidEvent {
  Lifecycle(Lifecycle)
  SurfaceCreated(Int32, Int32)
  SurfaceChanged(Int32, Int32)
  SurfaceDestroyed
  WorkerAttached
  UiTask
} derive(Eq, Debug)

///|
#deprecated("Use `==` instead", skip_current_package=true)
#doc(hidden)
pub extend AndroidEvent with Eq::{equal, not_equal}

///|
#deprecated("Use `Debug::to_repr` instead", skip_current_package=true)
#doc(hidden)
pub extend AndroidEvent with Debug::{to_repr}

///|
#cfg(not(target="native"))
pub fn application_context() -> @runtime.JavaObject raise @runtime.RuntimeError {
  raise @runtime.UnsupportedTarget
}

///|
#cfg(not(target="native"))
pub fn runtime_ready() -> Bool {
  false
}

///|
#cfg(not(target="native"))
pub fn post_to_ui() -> Unit raise @ajni.JniError {
  raise NativeFailure("Android UI callbacks are unsupported on this platform")
}

///|
#cfg(not(target="native"))
pub fn start_worker() -> Unit raise @ajni.JniError {
  raise NativeFailure("Android native workers are unsupported on this platform")
}

///|
#cfg(not(target="native"))
pub fn emit_for_test(kind : Int32, first : Int32, second : Int32) -> Unit {
  dispatch_native_event(kind, first, second)
}

///|
#cfg(not(target="native"))
fn ensure_callback_installed() -> Unit {
  ()
}
