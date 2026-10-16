///|
#cfg(not(target="native"))
pub fn host_open(
  _env : UInt64,
  class_loader? : UInt64 = 0,
) -> @runtime.Session raise @runtime.RuntimeError {
  let _ = class_loader
  raise @runtime.UnsupportedTarget
}

///|
#cfg(not(target="native"))
pub fn host_enter(_env : UInt64) -> Unit raise @runtime.RuntimeError {
  raise @runtime.UnsupportedTarget
}

///|
#cfg(not(target="native"))
pub fn host_leave() -> Unit raise @runtime.RuntimeError {
  raise @runtime.UnsupportedTarget
}

///|
#cfg(not(target="native"))
pub fn host_close(_env : UInt64) -> Unit raise @runtime.RuntimeError {
  raise @runtime.UnsupportedTarget
}

///|
#cfg(not(target="native"))
pub fn host_throw(_env : UInt64) -> Unit raise @runtime.RuntimeError {
  raise @runtime.UnsupportedTarget
}

///|
#cfg(not(target="native"))
pub fn register_companion(_env : UInt64) -> Unit raise @runtime.RuntimeError {
  raise @runtime.UnsupportedTarget
}
