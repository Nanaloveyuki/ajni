///|
extern "C" fn native_supported() -> Bool = "ajni_unsafe_supported"

///|
extern "C" fn native_host_open(env : UInt64, class_loader : UInt64) -> Int = "ajni_unsafe_host_open"

///|
extern "C" fn native_host_enter(env : UInt64) -> Int = "ajni_unsafe_host_enter"

///|
extern "C" fn native_host_leave() -> Unit = "ajni_unsafe_host_leave"

///|
extern "C" fn native_host_close(env : UInt64) -> Int = "ajni_unsafe_host_close"

///|
extern "C" fn native_host_throw(env : UInt64) -> Unit = "ajni_unsafe_host_throw"

///|
extern "C" fn native_register_companion(env : UInt64) -> Int = "ajni_unsafe_register_companion"
