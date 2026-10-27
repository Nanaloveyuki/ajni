///|
/// Dynamic-loader and JVM creation failures. Java operations use RuntimeError.
pub(all) suberror InvocationError {
  InvalidLibraryPath
  InvalidOption(String)
  LoadFailed(@dynlib.DynlibError)
  ResolveFailed(@dynlib.DynlibError)
  AlreadyAttempted
  CreateFailed(Int32)
  OutOfMemory
}

///|
#cfg(not(target="native"))
pub fn create(
  _library_path : String,
  _options : ArrayView[String],
) -> @runtime.Session raise {
  raise @runtime.UnsupportedTarget
}
