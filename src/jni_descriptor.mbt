///|
// Classfile CONSTANT_Utf8 lengths count UTF-16 code units in Modified UTF-8,
// not Unicode scalar values or ordinary UTF-8. Stop before the bound can grow.
fn mutf8_length_bounded(text : String, limit : Int32) -> Int32 raise JniError {
  if text.length() > limit {
    raise DescriptorTooLong
  }
  let mut bytes = 0
  for i in 0..<text.length() {
    let unit = text[i].to_int()
    bytes += if unit == 0 {
      2
    } else if unit <= 0x7F {
      1
    } else if unit <= 0x7FF {
      2
    } else {
      3
    }
    if bytes > limit {
      raise DescriptorTooLong
    }
  }
  bytes
}

///|
/// An internal JVM binary class name, for example `java/lang/String`.
pub struct JniClass {
  priv internal_name : String
  priv internal_name_bytes : Int32
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
/// Validate the internal name and reserve two MUTF-8 bytes for `L` and `;`.
/// Oversized names raise `DescriptorTooLong`; malformed names retain
/// `InvalidClassName`. The non-raising descriptor/object constructors are then
/// guaranteed to fit the 65,535-byte classfile limit.
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
  let internal_name_bytes = mutf8_length_bounded(internal_name, 65533)
  { internal_name, internal_name_bytes, }
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
  priv descriptor_bytes : Int32
  priv parameter_slots : Int32
  priv array_dimensions : Int32
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
  {
    descriptor: "Z",
    descriptor_bytes: 1,
    parameter_slots: 1,
    array_dimensions: 0,
  }
}

///|
pub fn JniType::byte() -> JniType {
  {
    descriptor: "B",
    descriptor_bytes: 1,
    parameter_slots: 1,
    array_dimensions: 0,
  }
}

///|
pub fn JniType::char() -> JniType {
  {
    descriptor: "C",
    descriptor_bytes: 1,
    parameter_slots: 1,
    array_dimensions: 0,
  }
}

///|
pub fn JniType::short() -> JniType {
  {
    descriptor: "S",
    descriptor_bytes: 1,
    parameter_slots: 1,
    array_dimensions: 0,
  }
}

///|
pub fn JniType::int() -> JniType {
  {
    descriptor: "I",
    descriptor_bytes: 1,
    parameter_slots: 1,
    array_dimensions: 0,
  }
}

///|
pub fn JniType::long() -> JniType {
  {
    descriptor: "J",
    descriptor_bytes: 1,
    parameter_slots: 2,
    array_dimensions: 0,
  }
}

///|
pub fn JniType::float() -> JniType {
  {
    descriptor: "F",
    descriptor_bytes: 1,
    parameter_slots: 1,
    array_dimensions: 0,
  }
}

///|
pub fn JniType::double() -> JniType {
  {
    descriptor: "D",
    descriptor_bytes: 1,
    parameter_slots: 2,
    array_dimensions: 0,
  }
}

///|
pub fn JniType::object(class : JniClass) -> JniType {
  {
    descriptor: class.descriptor(),
    descriptor_bytes: class.internal_name_bytes + 2,
    parameter_slots: 1,
    array_dimensions: 0,
  }
}

///|
pub fn JniType::array(element : JniType) -> JniType raise JniError {
  if element.array_dimensions >= 255 {
    raise TooManyArrayDimensions
  }
  if element.descriptor_bytes >= 65535 {
    raise DescriptorTooLong
  }
  {
    descriptor: "[\{element.descriptor}",
    descriptor_bytes: element.descriptor_bytes + 1,
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
/// Validation covers descriptor shape, MUTF-8 length, and up to 255 explicit
/// parameter slots, not a function-pointer ABI or an instance/static context.
/// Instance invocation also needs a slot for `this` and permits at most 254
/// explicit slots; current Kotlin native registrations are static.
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
  let result_descriptor = match return_type {
    Some(value) => value.descriptor
    None => "V"
  }
  let mut descriptor_bytes = 2 +
    (match return_type {
      Some(value) => value.descriptor_bytes
      None => 1
    })
  if descriptor_bytes > 65535 {
    raise DescriptorTooLong
  }
  let mut descriptor_units = 2 + result_descriptor.length()
  let mut parameter_slots = 0
  for parameter in parameters {
    parameter_slots += parameter.parameter_slots
    if parameter_slots > 255 {
      raise TooManyParameterSlots
    }
    descriptor_bytes += parameter.descriptor_bytes
    if descriptor_bytes > 65535 {
      raise DescriptorTooLong
    }
    descriptor_units += parameter.descriptor.length()
  }
  let builder = StringBuilder(size_hint=descriptor_units * 2)
  builder.write_char('(')
  for parameter in parameters {
    builder.write_string(parameter.descriptor)
  }
  builder.write_char(')')
  builder.write_string(result_descriptor)
  { descriptor: builder.to_string(), }
}

///|
pub fn JniMethod::descriptor(self : JniMethod) -> String {
  self.descriptor
}

///|
/// A native-method registration declaration without a raw function pointer.
/// It validates the name and descriptor, not registration linkage or ABI.
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
  ignore(mutf8_length_bounded(name, 65535))
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
