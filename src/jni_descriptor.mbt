///|
/// An internal JVM binary class name, for example `java/lang/String`.
pub struct JniClass {
  priv internal_name : String
} derive(Eq, Debug)

///|
#deprecated("Use `==` instead", skip_current_package=true)
#doc(hidden)
pub extend JniClass with Eq::{equal, not_equal}

///|
#deprecated("Use `Debug::to_repr` instead", skip_current_package=true)
#doc(hidden)
pub extend JniClass with Debug::{to_repr}

///|
pub fn JniClass::parse(internal_name : String) -> JniClass raise JniError {
  if internal_name.is_empty() ||
    internal_name.has_prefix("/") ||
    internal_name.has_suffix("/") ||
    internal_name.contains("//") ||
    internal_name.contains(".") ||
    internal_name.contains(";") ||
    internal_name.contains("[") ||
    internal_name.contains_code_unit(0) {
    raise InvalidClassName(internal_name)
  }
  { internal_name, }
}

///|
pub fn JniClass::internal_name(self : JniClass) -> String {
  self.internal_name
}

///|
pub fn JniClass::descriptor(self : JniClass) -> String {
  "L\{self.internal_name};"
}

///|
/// A non-void JNI value type. Its parameter-slot width is retained so method
/// descriptors cannot undercount `long` and `double`.
pub struct JniType {
  priv descriptor : String
  priv parameter_slots : Int
  priv array_dimensions : Int
} derive(Eq, Debug)

///|
#deprecated("Use `==` instead", skip_current_package=true)
#doc(hidden)
pub extend JniType with Eq::{equal, not_equal}

///|
#deprecated("Use `Debug::to_repr` instead", skip_current_package=true)
#doc(hidden)
pub extend JniType with Debug::{to_repr}

///|
pub fn JniType::boolean() -> JniType {
  { descriptor: "Z", parameter_slots: 1, array_dimensions: 0, }
}

///|
pub fn JniType::byte() -> JniType {
  { descriptor: "B", parameter_slots: 1, array_dimensions: 0, }
}

///|
pub fn JniType::char() -> JniType {
  { descriptor: "C", parameter_slots: 1, array_dimensions: 0, }
}

///|
pub fn JniType::short() -> JniType {
  { descriptor: "S", parameter_slots: 1, array_dimensions: 0, }
}

///|
pub fn JniType::int() -> JniType {
  { descriptor: "I", parameter_slots: 1, array_dimensions: 0, }
}

///|
pub fn JniType::long() -> JniType {
  { descriptor: "J", parameter_slots: 2, array_dimensions: 0, }
}

///|
pub fn JniType::float() -> JniType {
  { descriptor: "F", parameter_slots: 1, array_dimensions: 0, }
}

///|
pub fn JniType::double() -> JniType {
  { descriptor: "D", parameter_slots: 2, array_dimensions: 0, }
}

///|
pub fn JniType::object(class : JniClass) -> JniType {
  { descriptor: class.descriptor(), parameter_slots: 1, array_dimensions: 0, }
}

///|
pub fn JniType::array(element : JniType) -> JniType raise JniError {
  if element.array_dimensions >= 255 {
    raise TooManyArrayDimensions
  }
  {
    descriptor: "[\{element.descriptor}",
    parameter_slots: 1,
    array_dimensions: element.array_dimensions + 1,
  }
}

///|
pub fn JniType::descriptor(self : JniType) -> String {
  self.descriptor
}

///|
/// A validated JVM method descriptor. The default return type is `void`.
pub struct JniMethod {
  priv descriptor : String
} derive(Eq, Debug)

///|
#deprecated("Use `==` instead", skip_current_package=true)
#doc(hidden)
pub extend JniMethod with Eq::{equal, not_equal}

///|
#deprecated("Use `Debug::to_repr` instead", skip_current_package=true)
#doc(hidden)
pub extend JniMethod with Debug::{to_repr}

///|
pub fn JniMethod::new(
  parameters : ArrayView[JniType],
  return_type? : JniType,
) -> JniMethod raise JniError {
  let mut descriptor = "("
  let mut parameter_slots = 0
  for parameter in parameters {
    parameter_slots += parameter.parameter_slots
    if parameter_slots > 255 {
      raise TooManyParameterSlots
    }
    descriptor += parameter.descriptor
  }
  descriptor += ")"
  descriptor += match return_type {
    Some(value) => value.descriptor
    None => "V"
  }
  if descriptor.length() > 65535 {
    raise DescriptorTooLong
  }
  { descriptor, }
}

///|
pub fn JniMethod::descriptor(self : JniMethod) -> String {
  self.descriptor
}

///|
/// A native-method registration declaration without a raw function pointer.
pub struct NativeMethod {
  priv name : String
  priv signature : JniMethod
} derive(Eq, Debug)

///|
#deprecated("Use `==` instead", skip_current_package=true)
#doc(hidden)
pub extend NativeMethod with Eq::{equal, not_equal}

///|
#deprecated("Use `Debug::to_repr` instead", skip_current_package=true)
#doc(hidden)
pub extend NativeMethod with Debug::{to_repr}

///|
pub fn NativeMethod::new(
  name : String,
  signature : JniMethod,
) -> NativeMethod raise JniError {
  if name.is_empty() ||
    name.contains("/") ||
    name.contains(".") ||
    name.contains(";") ||
    name.contains("[") ||
    name.contains("<") ||
    name.contains(">") ||
    name.contains_code_unit(0) {
    raise InvalidNativeMethodName(name)
  }
  { name, signature, }
}

///|
pub fn NativeMethod::name(self : NativeMethod) -> String {
  self.name
}

///|
pub fn NativeMethod::descriptor(self : NativeMethod) -> String {
  self.signature.descriptor()
}
