///|
fn check_native_status() -> Unit raise RuntimeError {
  internal_check_status(native_last_status())
}

///|
/// Internal seam for the explicitly unsafe and optional host packages.
pub fn internal_check_status(status : Int32) -> Unit raise RuntimeError {
  match status {
    0 => ()
    1 => raise UnsupportedTarget
    2 => raise NotInitialized
    3 => raise Closed
    4 => raise WrongThread
    5 => raise Busy
    6 => raise InvalidArgument(native_error_message())
    7 => raise JavaThrown({ native: native_take_exception(), })
    _ => raise NativeFailure(native_error_message())
  }
}

///|
fn nullable_object(object : JavaObject?) -> NativeObject raise RuntimeError {
  match object {
    Some(object) => object.native
    None => {
      let result = native_null_object()
      check_native_status()
      result
    }
  }
}

///|
fn encode_value(value : JavaValue) -> NativeValue raise RuntimeError {
  let result = match value {
    Void => native_value_void()
    Boolean(value) => native_value_int(1, if value { 1L } else { 0L })
    Byte(value) => {
      if value < -128 || value > 127 {
        raise InvalidArgument("Java byte must be in -128..127")
      }
      native_value_int(2, value.to_int64())
    }
    Char(value) => {
      if value < 0 || value > 65535 {
        raise InvalidArgument("Java char must be in 0..65535")
      }
      native_value_int(3, value.to_int64())
    }
    Short(value) => {
      if value < -32768 || value > 32767 {
        raise InvalidArgument("Java short must be in -32768..32767")
      }
      native_value_int(4, value.to_int64())
    }
    Int(value) => native_value_int(5, value.to_int64())
    Long(value) => native_value_int(6, value)
    Float(value) => native_value_float(7, value.to_double())
    Double(value) => native_value_float(8, value)
    Object(value) => {
      let object = nullable_object(value)
      native_value_object(object)
    }
  }
  check_native_status()
  result
}

///|
fn decode_integer(value : NativeValue) -> Int64 raise RuntimeError {
  let result = native_value_integer(value)
  check_native_status()
  result
}

///|
fn decode_number(value : NativeValue) -> Double raise RuntimeError {
  let result = native_value_number(value)
  check_native_status()
  result
}

///|
fn decode_value(value : NativeValue) -> JavaValue raise RuntimeError {
  let tag = native_value_tag(value)
  check_native_status()
  match tag {
    0 => Void
    1 => Boolean(decode_integer(value) != 0L)
    2 => Byte(decode_integer(value).to_int())
    3 => Char(decode_integer(value).to_int())
    4 => Short(decode_integer(value).to_int())
    5 => Int(decode_integer(value).to_int())
    6 => Long(decode_integer(value))
    7 => Float(Float::from_double(decode_number(value)))
    8 => Double(decode_number(value))
    9 => {
      let object = native_value_get_object(value)
      check_native_status()
      let is_null = native_object_is_null(object)
      check_native_status()
      Object(if is_null != 0 { None } else { Some({ native: object, }) })
    }
    _ => raise NativeFailure("Native value has an unknown JNI type tag")
  }
}

///|
fn encode_args(values : ArrayView[JavaValue]) -> NativeArgs raise RuntimeError {
  let args = native_args_new()
  check_native_status()
  for value in values {
    if value is Void {
      raise InvalidArgument("Void is not a Java argument or array element")
    }
    let encoded = encode_value(value)
    ignore(native_args_push(args, encoded))
    check_native_status()
  }
  args
}

///|
fn decode_args(args : NativeArgs) -> Array[JavaValue] raise RuntimeError {
  let length = native_args_length(args)
  check_native_status()
  Array::makei(length, index => {
    let value = native_args_get(args, index)
    check_native_status()
    decode_value(value)
  })
}

///|
/// Returns the live session installed by a Java, Android or invocation host.
pub fn Session::current() -> Session raise RuntimeError {
  let native = native_current()
  check_native_status()
  { native, }
}

///|
/// Closes all derived handles; raises Busy during an active Java/managed entry.
pub fn Session::close(self : Session) -> Unit raise RuntimeError {
  ignore(native_session_close(self.native))
  check_native_status()
}

///|
pub fn Session::find_class(
  self : Session,
  name : @ajni.JniClass,
) -> JavaClass raise RuntimeError {
  let native = native_find_class(self.native, name.internal_name())
  check_native_status()
  { object: { native, }, }
}

///|
/// Preserves UTF-16 code units, including embedded NUL and unpaired surrogates.
pub fn Session::new_string(
  self : Session,
  value : String,
) -> JavaObject raise RuntimeError {
  let native = native_new_string(self.native, value)
  check_native_status()
  { native, }
}

///|
pub fn Session::new_primitive_array(
  self : Session,
  element_type : @ajni.JniType,
  length : Int32,
) -> JavaObject raise RuntimeError {
  let tag = match element_type.descriptor() {
    "Z" => 1
    "B" => 2
    "C" => 3
    "S" => 4
    "I" => 5
    "J" => 6
    "F" => 7
    "D" => 8
    _ =>
      raise InvalidArgument(
        "A primitive array requires a primitive element type",
      )
  }
  let native = native_array_new(self.native, tag, length)
  check_native_status()
  { native, }
}

///|
pub fn JavaObject::close(self : JavaObject) -> Unit raise RuntimeError {
  ignore(native_object_close(self.native))
  check_native_status()
}

///|
pub fn JavaObject::is_same(
  self : JavaObject,
  other : JavaObject,
) -> Bool raise RuntimeError {
  let result = native_object_same(self.native, other.native)
  check_native_status()
  result != 0
}

///|
/// Reads a java.lang.String, not an arbitrary object's Java toString method.
pub fn JavaObject::to_string_value(
  self : JavaObject,
) -> String raise RuntimeError {
  let result = native_read_string(self.native)
  check_native_status()
  result
}

///|
pub fn JavaObject::array_length(self : JavaObject) -> Int32 raise RuntimeError {
  let result = native_array_length(self.native)
  check_native_status()
  result
}

///|
pub fn JavaObject::get_element(
  self : JavaObject,
  index : Int32,
) -> JavaValue raise RuntimeError {
  let result = native_array_get(self.native, index)
  check_native_status()
  decode_value(result)
}

///|
pub fn JavaObject::set_element(
  self : JavaObject,
  index : Int32,
  value : JavaValue,
) -> Unit raise RuntimeError {
  if value is Void {
    raise InvalidArgument("Void is not a Java array element")
  }
  let encoded = encode_value(value)
  ignore(native_array_set(self.native, index, encoded))
  check_native_status()
}

///|
/// Copies the requested region; no Java memory remains pinned across this call.
pub fn JavaObject::read_region(
  self : JavaObject,
  start : Int32,
  length : Int32,
) -> Array[JavaValue] raise RuntimeError {
  let result = native_array_read(self.native, start, length)
  check_native_status()
  decode_args(result)
}

///|
pub fn JavaObject::write_region(
  self : JavaObject,
  start : Int32,
  values : ArrayView[JavaValue],
) -> Unit raise RuntimeError {
  let args = encode_args(values)
  ignore(native_array_write(self.native, start, args))
  check_native_status()
}

///|
pub fn JavaClass::instance_method(
  self : JavaClass,
  name : String,
  descriptor : @ajni.JniMethod,
) -> JavaMethod raise RuntimeError {
  let native = native_lookup(
    self.object.native,
    name,
    descriptor.descriptor(),
    0,
  )
  check_native_status()
  { native, }
}

///|
pub fn JavaClass::static_method(
  self : JavaClass,
  name : String,
  descriptor : @ajni.JniMethod,
) -> JavaMethod raise RuntimeError {
  let native = native_lookup(
    self.object.native,
    name,
    descriptor.descriptor(),
    1,
  )
  check_native_status()
  { native, }
}

///|
pub fn JavaClass::lookup_constructor(
  self : JavaClass,
  parameter_types : ArrayView[@ajni.JniType],
) -> JavaConstructor raise RuntimeError {
  let descriptor = @ajni.JniMethod::new(parameter_types) catch {
    _ => raise InvalidArgument("Constructor descriptor exceeds JVM limits")
  }
  let native = native_lookup(
    self.object.native,
    "<init>",
    descriptor.descriptor(),
    2,
  )
  check_native_status()
  { native, }
}

///|
pub fn JavaClass::instance_field(
  self : JavaClass,
  name : String,
  field_type : @ajni.JniType,
) -> JavaField raise RuntimeError {
  let native = native_lookup(
    self.object.native,
    name,
    field_type.descriptor(),
    3,
  )
  check_native_status()
  { native, }
}

///|
pub fn JavaClass::static_field(
  self : JavaClass,
  name : String,
  field_type : @ajni.JniType,
) -> JavaField raise RuntimeError {
  let native = native_lookup(
    self.object.native,
    name,
    field_type.descriptor(),
    4,
  )
  check_native_status()
  { native, }
}

///|
pub fn JavaClass::new_object_array(
  self : JavaClass,
  length : Int32,
  initial? : JavaObject,
) -> JavaObject raise RuntimeError {
  let initial = nullable_object(initial)
  let native = native_object_array_new(self.object.native, length, initial)
  check_native_status()
  { native, }
}

///|
/// Pass receiver only for an instance method; static calls reject a receiver.
pub fn JavaMethod::call(
  self : JavaMethod,
  args : ArrayView[JavaValue],
  receiver? : JavaObject,
) -> JavaValue raise RuntimeError {
  let receiver = nullable_object(receiver)
  let args = encode_args(args)
  let result = native_member_call(self.native, receiver, args)
  check_native_status()
  decode_value(result)
}

///|
pub fn JavaConstructor::new(
  self : JavaConstructor,
  args : ArrayView[JavaValue],
) -> JavaObject raise RuntimeError {
  let receiver = nullable_object(None)
  let args = encode_args(args)
  let result = native_member_call(self.native, receiver, args)
  check_native_status()
  match decode_value(result) {
    Object(Some(object)) => object
    _ => raise NativeFailure("Java constructor returned no object")
  }
}

///|
pub fn JavaField::get(
  self : JavaField,
  receiver? : JavaObject,
) -> JavaValue raise RuntimeError {
  let receiver = nullable_object(receiver)
  let result = native_field_get(self.native, receiver)
  check_native_status()
  decode_value(result)
}

///|
pub fn JavaField::set(
  self : JavaField,
  value : JavaValue,
  receiver? : JavaObject,
) -> Unit raise RuntimeError {
  if value is Void {
    raise InvalidArgument("Void is not a Java field value")
  }
  let receiver = nullable_object(receiver)
  let value = encode_value(value)
  ignore(native_field_set(self.native, receiver, value))
  check_native_status()
}

///|
fn callback_error(error : RuntimeError) -> NativeValue {
  let (status, throwable, message) = match error {
    UnsupportedTarget => (1, native_null_object(), "Unsupported JNI target")
    NotInitialized =>
      (2, native_null_object(), "JNI session is not initialized")
    Closed => (3, native_null_object(), "JNI resource is closed")
    WrongThread => (4, native_null_object(), "JNI owner thread is required")
    Busy => (5, native_null_object(), "JNI session is active")
    InvalidArgument(message) => (6, native_null_object(), message)
    JavaThrown(object) => (7, object.native, "")
    NativeFailure(message) => (8, native_null_object(), message)
  }
  native_callback_error(status, throwable, message)
}

///|
// C transfers a fresh owned closure and argument reference into this trampoline.
fn callback_trampoline(
  handler : (ArrayView[JavaValue]) -> JavaValue raise RuntimeError,
  args : NativeArgs,
) -> NativeValue {
  try {
    let values = decode_args(args)
    let value = handler(values[:])
    encode_value(value)
  } catch {
    error => callback_error(error)
  }
}

///|
/// The descriptor controls Java boxing; the callback does not implement a SAM.
pub fn Session::callback(
  self : Session,
  descriptor : @ajni.JniMethod,
  handler : (ArrayView[JavaValue]) -> JavaValue raise RuntimeError,
) -> JavaCallback raise RuntimeError {
  let native = native_callback(
    self.native,
    descriptor.descriptor(),
    fn(handler, args) { callback_trampoline(handler, args) },
    handler,
  )
  check_native_status()
  { object: { native, }, }
}

///|
pub fn JavaCallback::close(self : JavaCallback) -> Unit raise RuntimeError {
  ignore(native_callback_close(self.object.native))
  check_native_status()
}

///|
/// Requires a live JNIEnv for the owner thread and a valid reference from the
/// same VM. is_class requires a real jclass. Copies the borrowed reference.
pub fn unsafe_import_object(
  session : Session,
  env : UInt64,
  reference : UInt64,
  is_class : Bool,
) -> JavaObject raise RuntimeError {
  let native = native_import_object(
    session.native,
    env,
    reference,
    if is_class {
      1
    } else {
      0
    },
  )
  check_native_status()
  { native, }
}

///|
/// Requires a live owner-thread JNIEnv and a valid same-VM jclass reference.
pub fn unsafe_import_class(
  session : Session,
  env : UInt64,
  reference : UInt64,
) -> JavaClass raise RuntimeError {
  { object: unsafe_import_object(session, env, reference, true), }
}

///|
/// Requires the current owner-thread JNIEnv. The caller owns the returned JNI
/// local reference and must release it before its local frame ends.
pub fn unsafe_export_local(
  object : JavaObject,
  env : UInt64,
) -> UInt64 raise RuntimeError {
  let result = native_export_local(object.native, env)
  check_native_status()
  result
}

///|
/// Returns a borrowed JavaVM pointer; never destroy it or outlive the host VM.
pub fn unsafe_raw_vm(session : Session) -> UInt64 raise RuntimeError {
  let result = native_raw_vm(session.native)
  check_native_status()
  result
}

///|
/// Returns a borrowed JNIEnv valid only on the owner thread in this host entry.
pub fn unsafe_raw_env(session : Session) -> UInt64 raise RuntimeError {
  let result = native_raw_env(session.native)
  check_native_status()
  result
}

///|
/// pointer must name a live function with the exact JNIEXPORT/JNICALL ABI for
/// descriptor. Registration does not keep its library or code storage alive.
pub fn unsafe_register_native(
  class_object : JavaClass,
  name : String,
  descriptor : String,
  pointer : UInt64,
) -> Unit raise RuntimeError {
  ignore(
    native_register_native(
      class_object.object.native,
      name,
      descriptor,
      pointer,
    ),
  )
  check_native_status()
}
