///|
/// Raw pointers are UInt64 addresses. Invalid, stale, foreign-VM or wrong-ABI
/// values are caller bugs and can crash the process before a checked error.
/// The JNIEnv must belong to the current owner thread; references must be live.
pub fn import_object(
  session : @runtime.Session,
  env : UInt64,
  reference : UInt64,
) -> @runtime.JavaObject raise @runtime.RuntimeError {
  @runtime.unsafe_import_object(session, env, reference, false)
}

///|
/// The reference must denote a live jclass in the session's VM, not an object.
pub fn import_class(
  session : @runtime.Session,
  env : UInt64,
  reference : UInt64,
) -> @runtime.JavaClass raise @runtime.RuntimeError {
  @runtime.unsafe_import_class(session, env, reference)
}

///|
/// Returns a caller-owned local reference, valid only in the given Env/frame.
/// DeleteLocalRef or the host's local-frame exit must release the result.
pub fn export_local(
  object : @runtime.JavaObject,
  env : UInt64,
) -> UInt64 raise @runtime.RuntimeError {
  @runtime.unsafe_export_local(object, env)
}

///|
/// Borrows JavaVM without transferring ownership or granting DestroyJavaVM.
pub fn raw_vm(session : @runtime.Session) -> UInt64 raise @runtime.RuntimeError {
  @runtime.unsafe_raw_vm(session)
}

///|
/// Borrows the current owner thread's Env; never cache it for another thread.
pub fn raw_env(
  session : @runtime.Session,
) -> UInt64 raise @runtime.RuntimeError {
  @runtime.unsafe_raw_env(session)
}

///|
/// Registers a real JNIEXPORT/JNICALL trampoline. The pointer's ABI, signature,
/// code lifetime and pre-managed owner gate remain the consumer's obligation.
/// A descriptor cannot prove that an arbitrary address has the correct ABI.
pub fn register_native(
  class : @runtime.JavaClass,
  name : String,
  descriptor : String,
  function_pointer : UInt64,
) -> Unit raise @runtime.RuntimeError {
  @runtime.unsafe_register_native(class, name, descriptor, function_pointer)
}
