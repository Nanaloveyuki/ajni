///|
type NativeSession

///|
type NativeObject

///|
type NativeMember

///|
priv type NativeValue

///|
priv type NativeArgs

///|
/// A session and every handle derived from it belong to the host's owner thread.
pub struct Session {
  native : NativeSession
}

///|
/// Aliases share one close state. All Java references are global references.
pub struct JavaObject {
  native : NativeObject
}

///|
pub struct JavaClass {
  object : JavaObject
}

///|
pub struct JavaMethod {
  native : NativeMember
}

///|
pub struct JavaConstructor {
  native : NativeMember
}

///|
pub struct JavaField {
  native : NativeMember
}

///|
pub struct JavaCallback {
  object : JavaObject
}

///|
/// Byte, Char and Short use Int payloads and are range-checked before JNI use.
pub(all) enum JavaValue {
  Void
  Boolean(Bool)
  Byte(Int)
  Char(Int)
  Short(Int)
  Int(Int)
  Long(Int64)
  Float(Float)
  Double(Double)
  Object(JavaObject?)
}

///|
pub(all) suberror RuntimeError {
  UnsupportedTarget
  NotInitialized
  Closed
  WrongThread
  Busy
  InvalidArgument(String)
  JavaThrown(JavaObject)
  NativeFailure(String)
}

///|
pub fn JavaCallback::object(self : JavaCallback) -> JavaObject {
  self.object
}

///|
pub fn JavaClass::object(self : JavaClass) -> JavaObject {
  self.object
}

///|
#cfg(not(target="native"))
pub fn Session::current() -> Session raise RuntimeError {
  raise UnsupportedTarget
}

///|
#cfg(not(target="native"))
pub fn Session::close(_self : Session) -> Unit raise RuntimeError {
  raise UnsupportedTarget
}

///|
#cfg(not(target="native"))
pub fn Session::find_class(
  _self : Session,
  _name : @ajni.JniClass,
) -> JavaClass raise RuntimeError {
  raise UnsupportedTarget
}

///|
#cfg(not(target="native"))
pub fn Session::new_string(
  _self : Session,
  _value : String,
) -> JavaObject raise RuntimeError {
  raise UnsupportedTarget
}

///|
#cfg(not(target="native"))
pub fn Session::new_primitive_array(
  _self : Session,
  _element_type : @ajni.JniType,
  _length : Int,
) -> JavaObject raise RuntimeError {
  raise UnsupportedTarget
}

///|
#cfg(not(target="native"))
pub fn Session::callback(
  _self : Session,
  _descriptor : @ajni.JniMethod,
  _handler : (ArrayView[JavaValue]) -> JavaValue raise RuntimeError,
) -> JavaCallback raise RuntimeError {
  raise UnsupportedTarget
}

///|
#cfg(not(target="native"))
pub fn JavaObject::close(_self : JavaObject) -> Unit raise RuntimeError {
  raise UnsupportedTarget
}

///|
#cfg(not(target="native"))
pub fn JavaObject::is_same(
  _self : JavaObject,
  _other : JavaObject,
) -> Bool raise RuntimeError {
  raise UnsupportedTarget
}

///|
#cfg(not(target="native"))
pub fn JavaObject::to_string_value(
  _self : JavaObject,
) -> String raise RuntimeError {
  raise UnsupportedTarget
}

///|
#cfg(not(target="native"))
pub fn JavaObject::array_length(_self : JavaObject) -> Int raise RuntimeError {
  raise UnsupportedTarget
}

///|
#cfg(not(target="native"))
pub fn JavaObject::get_element(
  _self : JavaObject,
  _index : Int,
) -> JavaValue raise RuntimeError {
  raise UnsupportedTarget
}

///|
#cfg(not(target="native"))
pub fn JavaObject::set_element(
  _self : JavaObject,
  _index : Int,
  _value : JavaValue,
) -> Unit raise RuntimeError {
  raise UnsupportedTarget
}

///|
#cfg(not(target="native"))
pub fn JavaObject::read_region(
  _self : JavaObject,
  _start : Int,
  _length : Int,
) -> Array[JavaValue] raise RuntimeError {
  raise UnsupportedTarget
}

///|
#cfg(not(target="native"))
pub fn JavaObject::write_region(
  _self : JavaObject,
  _start : Int,
  _values : ArrayView[JavaValue],
) -> Unit raise RuntimeError {
  raise UnsupportedTarget
}

///|
#cfg(not(target="native"))
pub fn JavaClass::instance_method(
  _self : JavaClass,
  _name : String,
  _descriptor : @ajni.JniMethod,
) -> JavaMethod raise RuntimeError {
  raise UnsupportedTarget
}

///|
#cfg(not(target="native"))
pub fn JavaClass::static_method(
  _self : JavaClass,
  _name : String,
  _descriptor : @ajni.JniMethod,
) -> JavaMethod raise RuntimeError {
  raise UnsupportedTarget
}

///|
#cfg(not(target="native"))
pub fn JavaClass::lookup_constructor(
  _self : JavaClass,
  _parameter_types : ArrayView[@ajni.JniType],
) -> JavaConstructor raise RuntimeError {
  raise UnsupportedTarget
}

///|
#cfg(not(target="native"))
pub fn JavaClass::instance_field(
  _self : JavaClass,
  _name : String,
  _field_type : @ajni.JniType,
) -> JavaField raise RuntimeError {
  raise UnsupportedTarget
}

///|
#cfg(not(target="native"))
pub fn JavaClass::static_field(
  _self : JavaClass,
  _name : String,
  _field_type : @ajni.JniType,
) -> JavaField raise RuntimeError {
  raise UnsupportedTarget
}

///|
#cfg(not(target="native"))
pub fn JavaClass::new_object_array(
  _self : JavaClass,
  _length : Int,
  initial? : JavaObject,
) -> JavaObject raise RuntimeError {
  let _ = initial
  raise UnsupportedTarget
}

///|
#cfg(not(target="native"))
pub fn JavaMethod::call(
  _self : JavaMethod,
  _args : ArrayView[JavaValue],
  receiver? : JavaObject,
) -> JavaValue raise RuntimeError {
  let _ = receiver
  raise UnsupportedTarget
}

///|
#cfg(not(target="native"))
pub fn JavaConstructor::new(
  _self : JavaConstructor,
  _args : ArrayView[JavaValue],
) -> JavaObject raise RuntimeError {
  raise UnsupportedTarget
}

///|
#cfg(not(target="native"))
pub fn JavaField::get(
  _self : JavaField,
  receiver? : JavaObject,
) -> JavaValue raise RuntimeError {
  let _ = receiver
  raise UnsupportedTarget
}

///|
#cfg(not(target="native"))
pub fn JavaField::set(
  _self : JavaField,
  _value : JavaValue,
  receiver? : JavaObject,
) -> Unit raise RuntimeError {
  let _ = receiver
  raise UnsupportedTarget
}

///|
#cfg(not(target="native"))
pub fn JavaCallback::close(_self : JavaCallback) -> Unit raise RuntimeError {
  raise UnsupportedTarget
}

///|
#cfg(not(target="native"))
pub fn internal_check_status(_status : Int) -> Unit raise RuntimeError {
  raise UnsupportedTarget
}

///|
#cfg(not(target="native"))
pub fn unsafe_import_object(
  _session : Session,
  _env : UInt64,
  _reference : UInt64,
  _is_class : Bool,
) -> JavaObject raise RuntimeError {
  raise UnsupportedTarget
}

///|
#cfg(not(target="native"))
pub fn unsafe_import_class(
  _session : Session,
  _env : UInt64,
  _reference : UInt64,
) -> JavaClass raise RuntimeError {
  raise UnsupportedTarget
}

///|
#cfg(not(target="native"))
pub fn unsafe_export_local(
  _object : JavaObject,
  _env : UInt64,
) -> UInt64 raise RuntimeError {
  raise UnsupportedTarget
}

///|
#cfg(not(target="native"))
pub fn unsafe_raw_vm(_session : Session) -> UInt64 raise RuntimeError {
  raise UnsupportedTarget
}

///|
#cfg(not(target="native"))
pub fn unsafe_raw_env(_session : Session) -> UInt64 raise RuntimeError {
  raise UnsupportedTarget
}

///|
#cfg(not(target="native"))
pub fn unsafe_register_native(
  _class_object : JavaClass,
  _name : String,
  _descriptor : String,
  _pointer : UInt64,
) -> Unit raise RuntimeError {
  raise UnsupportedTarget
}
