///|
priv type NativeOptions

///|
extern "C" fn native_supported() -> Bool = "ajni_invocation_supported"

///|
extern "C" fn native_has_attempted() -> Bool = "ajni_invocation_has_attempted"

///|
extern "C" fn native_options_new(count : Int) -> NativeOptions = "ajni_invocation_options_new"

///|
#borrow(options, value)
extern "C" fn native_options_push(
  options : NativeOptions,
  value : Bytes,
) -> Int = "ajni_invocation_options_push"

///|
#borrow(options)
extern "C" fn native_create(options : NativeOptions, address : UInt64) -> Int = "ajni_invocation_create"

///|
#borrow(options)
extern "C" fn native_attempted(options : NativeOptions) -> Bool = "ajni_invocation_attempted"

///|
#borrow(options)
extern "C" fn native_jni_status(options : NativeOptions) -> Int = "ajni_invocation_jni_status"

///|
#borrow(options)
extern "C" fn native_runtime_status(options : NativeOptions) -> Int = "ajni_invocation_runtime_status"
