///|
/// The host must initialize MoonBit runtime and packages before this call.
/// Env and optional ClassLoader are live caller-validated JNI pointers.
/// This MoonBit wrapper is not a pre-managed foreign-thread admission gate;
/// native host glue must call ajni_rt_host_* before entering MoonBit at all.
pub fn host_open(
  env : UInt64,
  class_loader~ : UInt64 = 0,
) -> @runtime.Session raise @runtime.RuntimeError {
  @runtime.internal_check_status(native_host_open(env, class_loader))
  @runtime.Session::current()
}

///|
/// Admits owner-thread synchronous reentry. Every success needs one host_leave.
/// Foreign-thread entry must use the C gate before touching managed values.
pub fn host_enter(env : UInt64) -> Unit raise @runtime.RuntimeError {
  @runtime.internal_check_status(native_host_enter(env))
}

///|
/// Balances a successful host_enter on the same thread, including error paths.
pub fn host_leave() -> Unit raise @runtime.RuntimeError {
  if !native_supported() {
    raise @runtime.UnsupportedTarget
  }
  native_host_leave()
}

///|
/// Closes ajni resources at active depth zero; never destroys the host's VM.
pub fn host_close(env : UInt64) -> Unit raise @runtime.RuntimeError {
  @runtime.internal_check_status(native_host_close(env))
}

///|
/// Throws the native host's saved error into its Env. Return to Java immediately
/// afterwards; do not continue managed/JNI operations with an exception pending.
pub fn host_throw(env : UInt64) -> Unit raise @runtime.RuntimeError {
  if !native_supported() {
    raise @runtime.UnsupportedTarget
  }
  native_host_throw(env)
}

///|
/// Installs the fixed AjniCallback dispatcher after companion classes are loaded.
pub fn register_companion(env : UInt64) -> Unit raise @runtime.RuntimeError {
  @runtime.internal_check_status(native_register_companion(env))
}
