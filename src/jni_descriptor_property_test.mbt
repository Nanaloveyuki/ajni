///|
fn positive_mod(value : Int, modulus : Int) -> Int {
  let remainder = value % modulus
  if remainder < 0 {
    remainder + modulus
  } else {
    remainder
  }
}

///|
fn generated_class_name(raw : Array[Int]) -> String {
  let alphabet : ReadOnlyArray[Char] = [
    'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p',
    'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', 'A', 'B', 'C', 'D', 'E', 'F',
    'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V',
    'W', 'X', 'Y', 'Z', '_', '$', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
  ]
  let output : Array[Char] = ['p']
  for value in raw {
    if (value & 7) == 0 {
      output.push('/')
    }
    output.push(alphabet[positive_mod(value >> 3, alphabet.length())])
  }
  String::from_array(output)
}

///|
fn generated_native_name(raw : Array[Int]) -> String {
  let alphabet : ReadOnlyArray[Char] = [
    'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p',
    'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', 'A', 'B', 'C', 'D', 'E', 'F',
    'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V',
    'W', 'X', 'Y', 'Z', '_', '$', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
  ]
  let output : Array[Char] = ['n']
  for value in raw {
    output.push(alphabet[positive_mod(value, alphabet.length())])
  }
  String::from_array(output)
}

///|
fn generated_type(raw : (Int, Int, Int)) -> (JniType, String) {
  let class_name = generated_class_name([raw.1])
  let (base, descriptor) = match positive_mod(raw.0, 9) {
    0 => (JniType::boolean(), "Z")
    1 => (JniType::byte(), "B")
    2 => (JniType::char(), "C")
    3 => (JniType::short(), "S")
    4 => (JniType::int(), "I")
    5 => (JniType::long(), "J")
    6 => (JniType::float(), "F")
    7 => (JniType::double(), "D")
    _ => {
      let class = try! JniClass::parse(class_name)
      (JniType::object(class), "L\{class_name};")
    }
  }
  let mut value = base
  let mut expected = descriptor
  let depth = positive_mod(raw.2, 8)
  for _ in 0..<depth {
    value = try! JniType::array(value)
    expected = "[" + expected
  }
  (value, expected)
}

///|
fn invalid_class_name(raw : (Array[Int], Int)) -> String {
  let base = generated_class_name(raw.0)
  match positive_mod(raw.1, 6) {
    0 => "/" + base
    1 => base + "/"
    2 => "p//" + base
    3 => "p." + base
    4 => "p;" + base
    _ => "p[" + base
  }
}

///|
fn invalid_native_name(raw : (Array[Int], Int)) -> String {
  let base = generated_native_name(raw.0)
  match positive_mod(raw.1, 6) {
    0 => "/" + base
    1 => "." + base
    2 => ";" + base
    3 => "[" + base
    4 => "<" + base
    _ => ">" + base
  }
}

///|
fn array_depth_case(depth : Int) -> (Bool, String) {
  let mut value = JniType::int()
  let mut expected = "I"
  for _ in 0..<depth {
    try {
      value = JniType::array(value)
      expected = "[" + expected
    } catch {
      TooManyArrayDimensions => return (false, expected)
      _ => return (false, "")
    } noraise {
      _ => ()
    }
  }
  (true, value.descriptor())
}

///|
fn slot_case(raw : Array[Int]) -> (Int, Bool, String, String) {
  let parameters : Array[JniType] = []
  let mut slots = 0
  let mut parameter_descriptor = ""
  for value in raw {
    if (value & 1) == 0 {
      parameters.push(JniType::long())
      slots += 2
      parameter_descriptor += "J"
    } else {
      parameters.push(JniType::int())
      slots += 1
      parameter_descriptor += "I"
    }
  }
  let expected = "(" + parameter_descriptor + ")V"
  try JniMethod::new(parameters[:]) catch {
    TooManyParameterSlots => (slots, false, "", expected)
    _ => (slots, false, "", expected)
  } noraise {
    descriptor_value => (slots, true, descriptor_value.descriptor(), expected)
  }
}

///|
fn method_descriptor_context(input : (Array[(Int, Int, Int)], Int)) -> String {
  let parameter_values : Array[JniType] = []
  let mut parameter_descriptor = ""
  for raw in input.0 {
    let (value, descriptor) = generated_type(raw)
    parameter_values.push(value)
    parameter_descriptor += descriptor
  }
  let (return_type, return_descriptor) = generated_type(
    (input.1, input.1 >> 1, input.1 >> 2),
  )
  let (actual_descriptor, expected_return) = match positive_mod(input.1, 3) {
    0 => {
      let descriptor = (try! JniMethod::new(parameter_values[:])).descriptor()
      (descriptor, "V")
    }
    _ => {
      let descriptor = (try! JniMethod::new(parameter_values[:], return_type~)).descriptor()
      (descriptor, return_descriptor)
    }
  }
  actual_descriptor + " != (" + parameter_descriptor + ")" + expected_return
}

///|
test "quickcheck: class descriptors preserve generated valid names" {
  @quickcheck.check(
    (raw : Array[Int]) => {
      let name = generated_class_name(raw)
      let class = try! JniClass::parse(name)
      class.internal_name() == name && class.descriptor() == "L" + name + ";"
    },
    count=500,
    max_size=64,
    max_shrinks=100,
    seed=2026082501,
  )
}

///|
test "quickcheck: method descriptors match an independent type oracle" {
  @quickcheck.check(
    (input : (Array[(Int, Int, Int)], Int)) => {
      let parameter_values : Array[JniType] = []
      let mut parameter_descriptor = ""
      for raw in input.0 {
        let (value, descriptor) = generated_type(raw)
        parameter_values.push(value)
        parameter_descriptor += descriptor
      }
      let return_value = generated_type((input.1, input.1 >> 1, input.1 >> 2))
      let (return_type, return_descriptor) = return_value
      let (actual_method, expected_return) = match positive_mod(input.1, 3) {
        0 => (try! JniMethod::new(parameter_values[:]), "V")
        _ =>
          (
            try! JniMethod::new(parameter_values[:], return_type~),
            return_descriptor,
          )
      }
      actual_method.descriptor() ==
      "(" + parameter_descriptor + ")" + expected_return
    },
    counterexample_context=method_descriptor_context,
    count=500,
    max_size=48,
    max_shrinks=100,
    seed=2026082502,
  )
}

///|
test "quickcheck: native declarations preserve valid names and signatures" {
  @quickcheck.check(
    (raw : Array[Int]) => {
      let name = generated_native_name(raw)
      let signature = try! JniMethod::new([])
      let declaration = try! NativeMethod::new(name, signature)
      declaration.name() == name && declaration.descriptor() == "()V"
    },
    count=500,
    max_size=64,
    max_shrinks=100,
    seed=2026082503,
  )
}

///|
test "quickcheck: invalid class names are rejected" {
  @quickcheck.check(
    (raw : (Array[Int], Int)) => {
      try JniClass::parse(invalid_class_name(raw)) catch {
        InvalidClassName(_) => true
        _ => false
      } noraise {
        _ => false
      }
    },
    count=300,
    max_size=48,
    max_shrinks=100,
    seed=2026082504,
  )
}

///|
test "quickcheck: invalid native names are rejected" {
  @quickcheck.check(
    (raw : (Array[Int], Int)) => {
      let signature = try! JniMethod::new([])
      try NativeMethod::new(invalid_native_name(raw), signature) catch {
        InvalidNativeMethodName(_) => true
        _ => false
      } noraise {
        _ => false
      }
    },
    count=300,
    max_size=48,
    max_shrinks=100,
    seed=2026082505,
  )
}

///|
test "quickcheck: array dimensions stop at the JVM limit" {
  @quickcheck.check(
    (raw : Int) => {
      let depth = positive_mod(raw, 257)
      let (accepted, descriptor) = array_depth_case(depth)
      let expected_acceptance = depth <= 255
      accepted == expected_acceptance &&
      (!accepted || descriptor == String::make(depth, '[') + "I")
    },
    count=300,
    max_size=256,
    max_shrinks=100,
    seed=2026082508,
  )
}

///|
test "quickcheck: method parameter slots reject only over-limit inputs" {
  @quickcheck.check(
    (raw : Array[Int]) => {
      let (slots, accepted, actual, expected) = slot_case(raw)
      accepted == (slots <= 255) && (!accepted || actual == expected)
    },
    count=400,
    max_size=300,
    max_shrinks=100,
    seed=2026082509,
  )
}

///|
test "quickcheck: UTF-8 encode and decode preserve strings" {
  @quickcheck.check(
    (text : String) => decode_utf8(encode_utf8(text)[:]) == text,
    count=1000,
    max_size=256,
    max_shrinks=100,
    seed=2026082506,
  )
}

///|
test "quickcheck: arbitrary byte input is normalized to stable UTF-8" {
  @quickcheck.check(
    (bytes : Bytes) => {
      let text = decode_utf8(bytes[:])
      decode_utf8(encode_utf8(text)[:]) == text
    },
    count=1000,
    max_size=256,
    max_shrinks=100,
    seed=2026082507,
  )
}
