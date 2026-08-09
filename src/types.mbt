///|
/// JNI failures represented as values at the MoonBit boundary.
pub(all) suberror JniError {
  NotInitialized
  RuntimeDestroyed
  NativeFailure(String)
  JavaException(String)
  InvalidClassName(String)
  InvalidNativeMethodName(String)
  DescriptorTooLong
  TooManyParameterSlots
  TooManyArrayDimensions
} derive(Debug)
