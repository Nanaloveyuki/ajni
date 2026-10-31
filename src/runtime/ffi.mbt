///|
extern "C" fn native_current() -> NativeSession = "ajni_rt_current"

///|
#borrow(session)
extern "C" fn native_session_close(session : NativeSession) -> Int32 = "ajni_rt_session_close"

///|
extern "C" fn native_last_status() -> Int32 = "ajni_rt_last_status"

///|
extern "C" fn native_take_exception() -> NativeObject = "ajni_rt_take_exception"

///|
extern "C" fn native_error_message() -> String = "ajni_rt_error_message"

///|
extern "C" fn native_null_object() -> NativeObject = "ajni_rt_null_object"

///|
#borrow(object)
extern "C" fn native_object_close(object : NativeObject) -> Int32 = "ajni_rt_object_close"

///|
#borrow(left, right)
extern "C" fn native_object_same(
  left : NativeObject,
  right : NativeObject,
) -> Int32 = "ajni_rt_object_same"

///|
#borrow(object)
extern "C" fn native_object_is_null(object : NativeObject) -> Int32 = "ajni_rt_object_is_null"

///|
#borrow(session, name)
extern "C" fn native_find_class(
  session : NativeSession,
  name : String,
) -> NativeObject = "ajni_rt_find_class"

///|
#borrow(session, value)
extern "C" fn native_new_string(
  session : NativeSession,
  value : String,
) -> NativeObject = "ajni_rt_new_string"

///|
#borrow(object)
extern "C" fn native_read_string(object : NativeObject) -> String = "ajni_rt_read_string"

///|
#borrow(class_object, name, descriptor)
extern "C" fn native_lookup(
  class_object : NativeObject,
  name : String,
  descriptor : String,
  kind : Int32,
) -> NativeMember = "ajni_rt_lookup"

///|
#borrow(handle, receiver, args)
extern "C" fn native_member_call(
  handle : NativeMember,
  receiver : NativeObject,
  args : NativeArgs,
) -> NativeValue = "ajni_rt_member_call"

///|
#borrow(handle, receiver)
extern "C" fn native_field_get(
  handle : NativeMember,
  receiver : NativeObject,
) -> NativeValue = "ajni_rt_field_get"

///|
#borrow(handle, receiver, value)
extern "C" fn native_field_set(
  handle : NativeMember,
  receiver : NativeObject,
  value : NativeValue,
) -> Int32 = "ajni_rt_field_set"

///|
extern "C" fn native_value_int(tag : Int32, value : Int64) -> NativeValue = "ajni_rt_value_int"

///|
extern "C" fn native_value_float(tag : Int32, value : Double) -> NativeValue = "ajni_rt_value_float"

///|
#borrow(object)
extern "C" fn native_value_object(object : NativeObject) -> NativeValue = "ajni_rt_value_object"

///|
extern "C" fn native_value_void() -> NativeValue = "ajni_rt_value_void"

///|
#borrow(value)
extern "C" fn native_value_tag(value : NativeValue) -> Int32 = "ajni_rt_value_tag"

///|
#borrow(value)
extern "C" fn native_value_integer(value : NativeValue) -> Int64 = "ajni_rt_value_integer"

///|
#borrow(value)
extern "C" fn native_value_number(value : NativeValue) -> Double = "ajni_rt_value_number"

///|
#borrow(value)
extern "C" fn native_value_get_object(value : NativeValue) -> NativeObject = "ajni_rt_value_get_object"

///|
extern "C" fn native_args_new() -> NativeArgs = "ajni_rt_args_new"

///|
#borrow(args, value)
extern "C" fn native_args_push(args : NativeArgs, value : NativeValue) -> Int32 = "ajni_rt_args_push"

///|
#borrow(args)
extern "C" fn native_args_length(args : NativeArgs) -> Int32 = "ajni_rt_args_length"

///|
#borrow(args)
extern "C" fn native_args_get(args : NativeArgs, index : Int32) -> NativeValue = "ajni_rt_args_get"

///|
#borrow(session)
extern "C" fn native_array_new(
  session : NativeSession,
  primitive_tag : Int32,
  length : Int32,
) -> NativeObject = "ajni_rt_array_new"

///|
#borrow(object)
extern "C" fn native_array_length(object : NativeObject) -> Int32 = "ajni_rt_array_length"

///|
#borrow(object)
extern "C" fn native_array_get(
  object : NativeObject,
  index : Int32,
) -> NativeValue = "ajni_rt_array_get"

///|
#borrow(object, value)
extern "C" fn native_array_set(
  object : NativeObject,
  index : Int32,
  value : NativeValue,
) -> Int32 = "ajni_rt_array_set"

///|
#borrow(component_class, initial)
extern "C" fn native_object_array_new(
  component_class : NativeObject,
  length : Int32,
  initial : NativeObject,
) -> NativeObject = "ajni_rt_object_array_new"

///|
#borrow(object)
extern "C" fn native_array_read(
  object : NativeObject,
  start : Int32,
  length : Int32,
) -> NativeArgs = "ajni_rt_array_read"

///|
#borrow(object, values)
extern "C" fn native_array_write(
  object : NativeObject,
  start : Int32,
  values : NativeArgs,
) -> Int32 = "ajni_rt_array_write"

///|
#borrow(session, descriptor, trampoline)
#owned(closure)
extern "C" fn native_callback(
  session : NativeSession,
  descriptor : String,
  trampoline : FuncRef[
    ((ArrayView[JavaValue]) -> JavaValue raise RuntimeError, NativeArgs) -> NativeValue,
  ],
  closure : (ArrayView[JavaValue]) -> JavaValue raise RuntimeError,
) -> NativeObject = "ajni_rt_callback"

///|
#borrow(throwable, message)
extern "C" fn native_callback_error(
  status : Int32,
  throwable : NativeObject,
  message : String,
) -> NativeValue = "ajni_rt_callback_error"

///|
#borrow(object)
extern "C" fn native_callback_close(object : NativeObject) -> Int32 = "ajni_rt_callback_close"

///|
#borrow(session)
extern "C" fn native_import_object(
  session : NativeSession,
  env : UInt64,
  reference : UInt64,
  is_class : Int32,
) -> NativeObject = "ajni_rt_import_object"

///|
#borrow(object)
extern "C" fn native_export_local(
  object : NativeObject,
  env : UInt64,
) -> UInt64 = "ajni_rt_export_local"

///|
#borrow(session)
extern "C" fn native_raw_vm(session : NativeSession) -> UInt64 = "ajni_rt_raw_vm"

///|
#borrow(session)
extern "C" fn native_raw_env(session : NativeSession) -> UInt64 = "ajni_rt_raw_env"

///|
#borrow(class_object, name, descriptor)
extern "C" fn native_register_native(
  class_object : NativeObject,
  name : String,
  descriptor : String,
  pointer : UInt64,
) -> Int32 = "ajni_rt_register_native"
