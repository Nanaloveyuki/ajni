# ajni JNI

`Nanaloveyuki/ajni` provides checked JVM JNI declarations without an Android
dependency. It intentionally does not expose raw `JNIEnv*`, `jobject`, or
function pointers because their lifetime rules cannot be represented safely by
plain MoonBit values.

## JNI Descriptors

```mbt check
///|
test {
  let string = try! JniClass::parse("java/lang/String")
  let signature = try! JniMethod::new(
    [JniType::object(string)],
    return_type=JniType::boolean(),
  )
  let registration = try! NativeMethod::new("nativeAcceptsString", signature)
  assert_eq(registration.descriptor(), "(Ljava/lang/String;)Z")
}
```

`JniType` cannot represent `void`; arrays and method parameters are bounded
by JVM limits. Import `Nanaloveyuki/ajni/android` for Android lifecycle,
Surface, worker, and main-Looper callbacks. Browser support remains in the
optional `Nanaloveyuki/ajni/webview` package.
