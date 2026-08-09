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
