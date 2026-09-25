///|
test "JVM descriptors preserve class and primitive types" {
  let string_class = try! @ajni.JniClass::parse("java/lang/String")
  let string_type = @ajni.JniType::object(string_class)
  let strings = try! @ajni.JniType::array(string_type)
  let signature = try! @ajni.JniMethod::new(
    [@ajni.JniType::int(), @ajni.JniType::long(), strings],
    return_type=@ajni.JniType::boolean(),
  )
  inspect(signature.descriptor(), content="(IJ[Ljava/lang/String;)Z")
}

///|
test "void result does not become a parameter type" {
  let signature = try! @ajni.JniMethod::new([@ajni.JniType::double()])
  inspect(signature.descriptor(), content="(D)V")
}

///|
test "native methods retain a checked name and descriptor" {
  let signature = try! @ajni.JniMethod::new([])
  let native_method = try! @ajni.NativeMethod::new(
    "nativeInitialize", signature,
  )
  inspect(native_method.name(), content="nativeInitialize")
  inspect(native_method.descriptor(), content="()V")
}

///|
test "class and native method names reject descriptor punctuation" {
  try @ajni.JniClass::parse("java.lang.String") catch {
    InvalidClassName(name) => inspect(name, content="java.lang.String")
    _ => panic()
  } noraise {
    _ => panic()
  }
  try
    @ajni.NativeMethod::new("native/fail", try! @ajni.JniMethod::new([]))
  catch {
    InvalidNativeMethodName(name) => inspect(name, content="native/fail")
    _ => panic()
  } noraise {
    _ => panic()
  }
}

///|
test "descriptor limits reject oversized JVM shapes" {
  let mut type_ = @ajni.JniType::int()
  for _ in 0..<255 {
    type_ = try! @ajni.JniType::array(type_)
  }
  try @ajni.JniType::array(type_) catch {
    TooManyArrayDimensions => ()
    _ => panic()
  } noraise {
    _ => panic()
  }
  let parameters : Array[@ajni.JniType] = []
  for _ in 0..<128 {
    parameters.push(@ajni.JniType::long())
  }
  try @ajni.JniMethod::new(parameters[:]) catch {
    TooManyParameterSlots => ()
    _ => panic()
  } noraise {
    _ => panic()
  }
}
