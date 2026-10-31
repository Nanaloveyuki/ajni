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

///|
test "class names reserve object descriptor bytes at MUTF-8 boundaries" {
  let cases : Array[(Char, Int32, String)] = [
    ('a', 65533, ""),
    ('é', 32766, "a"),
    ('中', 21844, "a"),
    ('😀', 10922, "a"),
    ('\u{7F}', 65533, ""),
    ('\u{80}', 32766, "a"),
    ('\u{7FF}', 32766, "a"),
    ('\u{800}', 21844, "a"),
    ((0xD800).unsafe_to_char(), 21844, "a"),
    ((0xDC00).unsafe_to_char(), 21844, "a"),
  ]
  for (character, count, suffix) in cases {
    let name = String::make(count, character) + suffix
    let class = try! @ajni.JniClass::parse(name)
    assert_eq(class.internal_name(), name)
    assert_eq(class.descriptor(), "L" + name + ";")
    try @ajni.JniClass::parse(name + "a") catch {
      DescriptorTooLong => ()
      _ => panic()
    } noraise {
      _ => panic()
    }
  }
  for count in [65534, 65535, 65536] {
    try @ajni.JniClass::parse(String::make(count, 'a')) catch {
      DescriptorTooLong => ()
      _ => panic()
    } noraise {
      _ => panic()
    }
  }
}

///|
test "native method names use the full MUTF-8 name byte limit" {
  let signature = try! @ajni.JniMethod::new([])
  let cases : Array[(Char, Int32, String)] = [
    ('a', 65535, ""),
    ('é', 32767, "a"),
    ('中', 21845, ""),
    ('😀', 10922, "aaa"),
    ('\u{7F}', 65535, ""),
    ('\u{80}', 32767, "a"),
    ('\u{7FF}', 32767, "a"),
    ('\u{800}', 21845, ""),
    ((0xD800).unsafe_to_char(), 21845, ""),
    ((0xDC00).unsafe_to_char(), 21845, ""),
  ]
  for (character, count, suffix) in cases {
    let name = String::make(count, character) + suffix
    let registration = try! @ajni.NativeMethod::new(name, signature)
    assert_eq(registration.name(), name)
    try @ajni.NativeMethod::new(name + "a", signature) catch {
      DescriptorTooLong => ()
      _ => panic()
    } noraise {
      _ => panic()
    }
  }
}

///|
test "method descriptors count encoding bytes including their framing" {
  let cases : Array[(Char, Int32, String)] = [
    ('a', 65530, ""),
    ('é', 32765, ""),
    ('中', 21843, "a"),
    ('😀', 10921, "aaaa"),
    ('\u{7F}', 65530, ""),
    ('\u{80}', 32765, ""),
    ('\u{7FF}', 32765, ""),
    ('\u{800}', 21843, "a"),
    ((0xD800).unsafe_to_char(), 21843, "a"),
    ((0xDC00).unsafe_to_char(), 21843, "a"),
  ]
  for (character, count, suffix) in cases {
    let name = String::make(count, character) + suffix
    let type_ = @ajni.JniType::object(try! @ajni.JniClass::parse(name))
    let signature = try! @ajni.JniMethod::new([type_])
    assert_eq(signature.descriptor(), "(L" + name + ";)V")
    let longer = @ajni.JniType::object(try! @ajni.JniClass::parse(name + "a"))
    try @ajni.JniMethod::new([longer]) catch {
      DescriptorTooLong => ()
      _ => panic()
    } noraise {
      _ => panic()
    }
  }
  let result_name = String::make(65531, 'a')
  let result = @ajni.JniType::object(try! @ajni.JniClass::parse(result_name))
  let signature = try! @ajni.JniMethod::new([], return_type=result)
  assert_eq(signature.descriptor(), "()L" + result_name + ";")
  let longer_result = @ajni.JniType::object(
    try! @ajni.JniClass::parse(result_name + "a"),
  )
  try @ajni.JniMethod::new([], return_type=longer_result) catch {
    DescriptorTooLong => ()
    _ => panic()
  } noraise {
    _ => panic()
  }
}

///|
test "array prefixes cannot exceed the descriptor byte limit" {
  let type_ = @ajni.JniType::object(
    try! @ajni.JniClass::parse(String::make(65532, 'a')),
  )
  let array = try! @ajni.JniType::array(type_)
  assert_eq(array.descriptor(), "[" + type_.descriptor())
  try @ajni.JniType::array(array) catch {
    DescriptorTooLong => ()
    _ => panic()
  } noraise {
    _ => panic()
  }
  let maximum_object = @ajni.JniType::object(
    try! @ajni.JniClass::parse(String::make(65533, 'a')),
  )
  try @ajni.JniType::array(maximum_object) catch {
    DescriptorTooLong => ()
    _ => panic()
  } noraise {
    _ => panic()
  }
  let parameter = try! @ajni.JniType::array(
    @ajni.JniType::object(try! @ajni.JniClass::parse(String::make(65529, 'a'))),
  )
  let signature = try! @ajni.JniMethod::new([parameter])
  assert_eq(signature.descriptor().length(), 65535)
  let longer_parameter = try! @ajni.JniType::array(parameter)
  try @ajni.JniMethod::new([longer_parameter]) catch {
    DescriptorTooLong => ()
    _ => panic()
  } noraise {
    _ => panic()
  }
}

///|
test "individually legal parameters can exceed aggregate descriptor bytes" {
  // This is 66,007 MUTF-8 bytes despite only 22,007 UTF-16 code units.
  let name = String::make(11000, '中')
  let parameter = @ajni.JniType::object(try! @ajni.JniClass::parse(name))
  ignore(try! @ajni.JniMethod::new([parameter]))
  try @ajni.JniMethod::new([parameter, parameter]) catch {
    DescriptorTooLong => ()
    _ => panic()
  } noraise {
    _ => panic()
  }
  try @ajni.JniMethod::new([parameter], return_type=parameter) catch {
    DescriptorTooLong => ()
    _ => panic()
  } noraise {
    _ => panic()
  }
  // The formerly accepted 66,005-byte one-parameter shape fails at class parse.
  try @ajni.JniClass::parse(String::make(22000, '中')) catch {
    DescriptorTooLong => ()
    _ => panic()
  } noraise {
    _ => panic()
  }
}

///|
test "NUL remains invalid in names regardless of MUTF-8 width" {
  let name = "name\u{00}tail"
  try @ajni.JniClass::parse(name) catch {
    InvalidClassName(value) => assert_eq(value, name)
    _ => panic()
  } noraise {
    _ => panic()
  }
  try @ajni.NativeMethod::new(name, try! @ajni.JniMethod::new([])) catch {
    InvalidNativeMethodName(value) => assert_eq(value, name)
    _ => panic()
  } noraise {
    _ => panic()
  }
}

///|
test "255 explicit parameter slots remain valid without instance context" {
  let parameters : Array[@ajni.JniType] = []
  for _ in 0..<127 {
    parameters.push(@ajni.JniType::long())
  }
  parameters.push(@ajni.JniType::int())
  let signature = try! @ajni.JniMethod::new(parameters[:])
  assert_eq(signature.descriptor(), "(" + String::make(127, 'J') + "I)V")
  let declaration = try! @ajni.NativeMethod::new("nativeStatic", signature)
  assert_eq(declaration.descriptor(), signature.descriptor())
  parameters.push(@ajni.JniType::int())
  try @ajni.JniMethod::new(parameters[:]) catch {
    TooManyParameterSlots => ()
    _ => panic()
  } noraise {
    _ => panic()
  }
}
