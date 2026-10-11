# ajni JNI

`Nanaloveyuki/ajni` provides checked JVM JNI declarations without an Android
dependency. It intentionally does not expose raw `JNIEnv*`, `jobject`, or
function pointers because their lifetime rules cannot be represented safely by
plain MoonBit values.

## JNI Descriptors

```mbt check
///|
test {
  let string = try! @ajni.JniClass::parse("java/lang/String")
  let signature = try! @ajni.JniMethod::new(
    [@ajni.JniType::object(string)],
    return_type=@ajni.JniType::boolean(),
  )
  let registration = try! @ajni.NativeMethod::new(
    "nativeAcceptsString", signature,
  )
  assert_eq(registration.descriptor(), "(Ljava/lang/String;)Z")
}
```

`JniType` cannot represent `void`. Names and descriptors obey the classfile
65,535-byte **Modified UTF-8** limit, not UTF-16 length or ordinary UTF-8 length:
each UTF-16 code unit uses two bytes for NUL, one for U+0001–U+007F, two for
U+0080–U+07FF, and three otherwise. A supplementary character therefore uses
six bytes for its surrogate pair. Names still reject NUL as invalid syntax.

`JniClass::parse` reserves two bytes for the `L`/`;` framing, accepting at most
65,533 encoded name bytes so its non-raising `descriptor` and `JniType::object`
constructors always produce valid-sized descriptors. Native-method names may
use all 65,535 bytes. Oversized names, array prefixes, and the complete method
descriptor (including parentheses and return type) raise `DescriptorTooLong`;
malformed names retain `InvalidClassName` or `InvalidNativeMethodName`.

Arrays permit at most 255 dimensions. Method declarations permit at most 255
**explicit parameter slots**, counting `long` and `double` as two slots and
arrays as one. Static methods can use all 255; instance/interface invocation
also counts the implicit `this`, leaving at most 254 explicit slots. The
descriptor does not encode that invocation context, and the current Kotlin
native registrations are static. `JniMethod`/`NativeMethod` validate declaration
shape and limits, not function-pointer ABI, linkage, or every invocation form.

Import `Nanaloveyuki/ajni/android` for Android lifecycle, Surface, worker, and
main-Looper callbacks. Browser support remains in the optional
`Nanaloveyuki/ajni/webview` package.
