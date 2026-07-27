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
/// Events delivered from the Android bridge. Surface events are always emitted
/// on the Android UI thread; worker events are emitted by an attached native
/// thread or by the UI task it schedules.
pub(all) enum AndroidEvent {
  Lifecycle(Lifecycle)
  SurfaceCreated(Int, Int)
  SurfaceChanged(Int, Int)
  SurfaceDestroyed
  WorkerAttached
  UiTask
} derive(Eq, Debug)

///|
/// JNI failures are represented as values at the MoonBit boundary. The C
/// bridge clears a captured Java exception before returning this error.
pub(all) suberror JniError {
  NotInitialized
  RuntimeDestroyed
  NativeFailure(String)
  JavaException(String)
} derive(Debug)

///|
/// A Java object retained past the JNI call that created it.
///
/// The raw handle is private so a local reference cannot be accidentally
/// retained as a `GlobalRef`. Creation and destruction are owned by the C
/// bridge and are always paired with `NewGlobalRef`/`DeleteGlobalRef`.
pub struct GlobalRef {
  priv raw : UInt64
} derive(Eq, Debug)

///|
/// Creates a wrapper for a `NewGlobalRef` result owned by the native bridge.
/// Application code should receive these from a bridge API rather than creating
/// one from a raw handle.
pub fn GlobalRef::from_raw_for_bridge(raw : UInt64) -> GlobalRef {
  { raw, }
}

///|
pub fn GlobalRef::is_null(self : GlobalRef) -> Bool {
  self.raw == 0UL
}

///|
/// A local-reference frame. It is a scope token only: callers must not retain
/// object handles created inside a frame after the associated callback returns.
pub struct LocalFrame {
  priv capacity : Int
} derive(Eq, Debug)

///|
pub fn LocalFrame::new(capacity? : Int = 16) -> LocalFrame {
  { capacity: if capacity < 1 { 1 } else { capacity } }
}
