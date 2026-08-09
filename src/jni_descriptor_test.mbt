///|
test "JVM descriptors preserve class and primitive types" {
  let string_class = try! JniClass::parse("java/lang/String")
  let string_type = JniType::object(string_class)
  let strings = try! JniType::array(string_type)
  let signature = try! JniMethod::new(
    [JniType::int(), JniType::long(), strings],
    return_type=JniType::boolean(),
  )
  inspect(signature.descriptor(), content="(IJ[Ljava/lang/String;)Z")
}

///|
test "void result does not become a parameter type" {
  let signature = try! JniMethod::new([JniType::double()])
  inspect(signature.descriptor(), content="(D)V")
}

///|
test "native methods retain a checked name and descriptor" {
  let signature = try! JniMethod::new([])
  let native_method = try! NativeMethod::new("nativeInitialize", signature)
  inspect(native_method.name(), content="nativeInitialize")
  inspect(native_method.descriptor(), content="()V")
}

///|
test "class and native method names reject descriptor punctuation" {
  try JniClass::parse("java.lang.String") catch {
    InvalidClassName(name) => inspect(name, content="java.lang.String")
    _ => panic()
  } noraise {
    _ => panic()
  }
  try NativeMethod::new("native/fail", try! JniMethod::new([])) catch {
    InvalidNativeMethodName(name) => inspect(name, content="native/fail")
    _ => panic()
  } noraise {
    _ => panic()
  }
}

///|
test "descriptor limits reject oversized JVM shapes" {
  let mut type_ = JniType::int()
  for _ in 0..<255 {
    type_ = try! JniType::array(type_)
  }
  try JniType::array(type_) catch {
    TooManyArrayDimensions => ()
    _ => panic()
  } noraise {
    _ => panic()
  }
  let parameters : Array[JniType] = []
  for _ in 0..<128 {
    parameters.push(JniType::long())
  }
  try JniMethod::new(parameters[:]) catch {
    TooManyParameterSlots => ()
    _ => panic()
  } noraise {
    _ => panic()
  }
}
