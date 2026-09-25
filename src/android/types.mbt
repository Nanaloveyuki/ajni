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
  SurfaceCreated(Int, Int)
  SurfaceChanged(Int, Int)
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
