///|
/// JNI failures represented as values at the MoonBit boundary.
#derive(Debug)
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
}

///|
#deprecated("Use `Debug::to_repr` instead", skip_current_package=true)
#doc(hidden)
pub extend JniError with Debug::{to_repr}
