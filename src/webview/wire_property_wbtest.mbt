///|
fn property_field_name(index : Int) -> String {
  let output : Array[Char] = ['f']
  for _ in 0..<(index + 1) {
    output.push('a')
  }
  String::from_array(output)
}

///|
fn property_fields(values : Array[String]) -> Array[(String, String)] {
  let fields : Array[(String, String)] = []
  for index, value in values {
    fields.push((property_field_name(index), value))
  }
  fields
}

///|
fn fields_match(
  expected : Array[(String, String)],
  actual : Map[String, String],
) -> Bool {
  if expected.length() != actual.length() {
    return false
  }
  for field in expected {
    match actual.get(field.0) {
      Some(value) => if value != field.1 { return false }
      None => return false
    }
  }
  true
}

///|
fn malformed_wire(input : (String, Int)) -> String {
  let field = property_field_name(positive_mod(input.1, 8))
  let value = base64url(@ajni.encode_utf8(input.0))
  match positive_mod(input.1, 5) {
    0 => "=" + value
    1 => field + "=%"
    2 => field + "=" + value + "&" + field + "=" + value
    3 => field
    _ => field + "=" + value + "&broken"
  }
}

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
test "quickcheck: base64url round trips arbitrary UTF-8 text" {
  @quickcheck.check(
    (text : String) => {
      decode_base64url(base64url(@ajni.encode_utf8(text))) == Some(text)
    },
    count=1000,
    max_size=256,
    max_shrinks=100,
    seed=2026082511,
  )
}

///|
test "quickcheck: field encoding round trips unique valid fields" {
  @quickcheck.check(
    (values : Array[String]) => {
      let fields = property_fields(values)
      match decode_fields(encode_fields(fields)) {
        Some(decoded) => fields_match(fields, decoded)
        None => false
      }
    },
    count=750,
    max_size=32,
    max_shrinks=100,
    seed=2026082512,
  )
}

///|
test "quickcheck: malformed field wires are rejected" {
  @quickcheck.check(
    (input : (String, Int)) => decode_fields(malformed_wire(input)) is None,
    count=500,
    max_size=128,
    max_shrinks=100,
    seed=2026082513,
  )
}

///|
test "quickcheck: structured page messages preserve arbitrary payloads" {
  @quickcheck.check(
    (input : (String, String, Bool)) => {
      let main_frame = if input.2 { "1" } else { "0" }
      let wire = encode_fields([
        ("body", input.0),
        ("origin", input.1),
        ("main_frame", main_frame),
      ])
      match decode_page_message(@ajni.encode_utf8(wire)) {
        Some(PageMessage(body, origin, value)) =>
          body == input.0 && origin == input.1 && value == input.2
        _ => false
      }
    },
    count=750,
    max_size=128,
    max_shrinks=100,
    seed=2026082514,
  )
}

///|
test "quickcheck: resource limits accept exactly non-negative values" {
  @quickcheck.check(
    (input : (Int, Int, Int, Int)) => {
      let limits = WebViewResourceLimits::new(
        max_page_message_bytes=input.0,
        max_pending_asset_requests=input.1,
        max_asset_response_bytes=input.2,
        asset_response_timeout_millis=input.3,
      )
      valid_resource_limits(limits) ==
      (input.0 >= 0 && input.1 >= 0 && input.2 >= 0 && input.3 >= 0)
    },
    count=500,
    max_size=128,
    max_shrinks=100,
    seed=2026082515,
  )
}
