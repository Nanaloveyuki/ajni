///|
// A JVM can keep executing native library code even when creation reports failure.
let retained_libraries : Array[@dynlib.Library] = []

///|
fn full_library_path(path : String) -> Bool {
  if path.is_empty() || path.contains_code_unit(0) {
    return false
  }
  path.has_prefix("/") ||
  path.has_prefix("\\\\") ||
  (
    path.length() >= 3 &&
    ((path[0] >= 65 && path[0] <= 90) || (path[0] >= 97 && path[0] <= 122)) &&
    path[1] == 58 &&
    (path[2] == 47 || path[2] == 92)
  )
}

///|
/// Creates the process's sole JVM from a full dynamic-library path.
///
/// Options are passed unchanged as UTF-8, with ignoreUnrecognized=false.
/// Calling JNI_CreateJavaVM consumes admission even if it fails. Its library is
/// never unloaded afterwards; Session.close releases ajni resources, not the VM.
/// MoonBit runtime/package initialization must already have run on this thread.
/// If companion registration fails after session initialization, Session.current
/// exposes the initialized session so the host can explicitly close it.
pub fn create(
  library_path : String,
  options : ArrayView[String],
) -> @runtime.Session raise {
  if !native_supported() {
    raise @runtime.UnsupportedTarget
  }
  if native_has_attempted() {
    raise AlreadyAttempted
  }
  if !full_library_path(library_path) {
    raise InvalidLibraryPath
  }
  let native_options = native_options_new(options.length())
  for option in options {
    if option.contains_code_unit(0) {
      raise InvalidOption(option)
    }
    let status = native_options_push(native_options, @utf8.encode(option))
    if status == 2 {
      raise OutOfMemory
    }
    if status != 0 {
      raise InvalidOption(option)
    }
  }
  let library = match @dynlib.load(library_path) {
    Ok(library) => library
    Err(error) => raise LoadFailed(error)
  }
  let address = match library.resolve("JNI_CreateJavaVM") {
    Ok(symbol) =>
      match symbol.address() {
        Ok(address) => address
        Err(error) => {
          ignore(library.close())
          raise ResolveFailed(error)
        }
      }
    Err(error) => {
      ignore(library.close())
      raise ResolveFailed(error)
    }
  }
  let status = native_create(native_options, address)
  if native_attempted(native_options) {
    retained_libraries.push(library)
  } else {
    ignore(library.close())
  }
  match status {
    0 => @runtime.Session::current()
    1 => raise AlreadyAttempted
    2 => raise OutOfMemory
    4 => raise CreateFailed(native_jni_status(native_options))
    5 => {
      @runtime.internal_check_status(native_runtime_status(native_options))
      raise @runtime.NativeFailure(
        "JVM host initialization returned no diagnostic",
      )
    }
    _ => raise @runtime.NativeFailure("Invalid JVM invocation state")
  }
}
