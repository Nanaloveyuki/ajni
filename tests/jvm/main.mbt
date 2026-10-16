///|
#borrow(message)
extern "c" fn fixture_failure(message : String) -> Unit = "ajni_fixture_failure"

///|
extern "c" fn fixture_entry() -> Unit = "ajni_fixture_entry"

///|
let previous_session : Ref[@rt.Session?] = Ref(None)

///|
let previous_object : Ref[@rt.JavaObject?] = Ref(None)

///|
fn expect_invalid(action : () -> Unit raise @rt.RuntimeError, message : String) -> Unit raise @rt.RuntimeError {
  let rejected = try {
    action()
    false
  } catch {
    @rt.InvalidArgument(_) => true
    _ => false
  }
  require(rejected, message)
}

///|
fn require(condition : Bool, message : String) -> Unit raise @rt.RuntimeError {
  if !condition {
    raise @rt.NativeFailure(message)
  }
}

///|
fn integer(value : @rt.JavaValue) -> Int raise @rt.RuntimeError {
  match value {
    Int(value) => value
    _ => raise @rt.NativeFailure("Expected Java int")
  }
}

///|
fn object(value : @rt.JavaValue) -> @rt.JavaObject raise @rt.RuntimeError {
  match value {
    Object(Some(value)) => value
    _ => raise @rt.NativeFailure("Expected non-null Java object")
  }
}

///|
fn same_primitive(left : @rt.JavaValue, right : @rt.JavaValue) -> Bool {
  match (left, right) {
    (Boolean(x), Boolean(y)) => x == y
    (Byte(x), Byte(y)) => x == y
    (Char(x), Char(y)) => x == y
    (Short(x), Short(y)) => x == y
    (Int(x), Int(y)) => x == y
    (Long(x), Long(y)) => x == y
    (Float(x), Float(y)) => x == y
    (Double(x), Double(y)) => x == y
    _ => false
  }
}

///|
fn fixture_class(session : @rt.Session) -> @rt.JavaClass raise {
  session.find_class(@jni.JniClass::parse("dev/nanaloveyuki/ajni/fixture/Fixture"))
}

///|
fn setup() -> Unit raise {
  let session = @rt.Session::current()
  let fixture = fixture_class(session)
  let int_type = @jni.JniType::int()
  let string_type = @jni.JniType::object(@jni.JniClass::parse("java/lang/String"))
  let callback_type = @jni.JniType::object(@jni.JniClass::parse("dev/nanaloveyuki/ajni/AjniCallback"))
  let throwable_type = @jni.JniType::object(@jni.JniClass::parse("java/lang/RuntimeException"))
  let text = object(fixture.static_field("text", string_type).get())
  let original = text.to_string_value()
  require(original.length() == 9, "UTF-16 length")
  require(original[0] == 65 && original[1] == 0, "NUL preserved")
  require(original[2] == 0x4e2d && original[3] == 0x6587, "CJK preserved")
  require(original[4] == 0xd83d && original[5] == 0xde00, "Emoji preserved")
  require(original[6] == 0xd800 && original[8] == 0xdc00, "Unpaired UTF-16 preserved")
  let copied = session.new_string(original)
  require(copied.to_string_value() == original, "String round trip")
  previous_session.val = Some(session)
  previous_object.val = Some(copied)
  let ctor = fixture.lookup_constructor([int_type, string_type])
  let instance = ctor.new([@rt.Int(10), @rt.Object(Some(copied))])
  let add = fixture.instance_method("add", @jni.JniMethod::new([int_type], return_type=int_type))
  require(integer(add.call([@rt.Int(5)], receiver=instance)) == 15, "Instance invocation")
  let twice = fixture.static_method("twice", @jni.JniMethod::new([int_type], return_type=int_type))
  require(integer(twice.call([@rt.Int(21)])) == 42, "Static invocation")
  expect_invalid(() => { ignore(add.call([@rt.Int(1)], receiver=copied)) }, "Invalid instance receiver")
  let unicode_name = @jni.JniClass::parse("dev/nanaloveyuki/ajni/fixture/Fixture$中文")
  let unicode_type = @jni.JniType::object(unicode_name)
  let unicode_class = session.find_class(unicode_name)
  let unicode_object = unicode_class.lookup_constructor([int_type]).new([@rt.Int(40)])
  let unicode_method = unicode_class.instance_method("增加", @jni.JniMethod::new([int_type], return_type=int_type))
  require(integer(unicode_method.call([@rt.Int(2)], receiver=unicode_object)) == 42, "Unicode class and method")
  require(integer(unicode_class.instance_field("数值", int_type).get(receiver=unicode_object)) == 42, "Unicode field")
  let unicode_echo = fixture.static_method("回声", @jni.JniMethod::new([unicode_type], return_type=unicode_type))
  require(object(unicode_echo.call([@rt.Object(Some(unicode_object))])).is_same(unicode_object), "Unicode descriptor")
  let primitive_cases : Array[(@jni.JniType, String, @rt.JavaValue)] = [
    (@jni.JniType::boolean(), "booleanValue", @rt.Boolean(true)),
    (@jni.JniType::byte(), "byteValue", @rt.Byte(-128)),
    (@jni.JniType::char(), "charValue", @rt.Char(0xd800)),
    (@jni.JniType::short(), "shortValue", @rt.Short(-32768)),
    (@jni.JniType::long(), "longValue", @rt.Long(9007199254740993L)),
    (@jni.JniType::float(), "floatValue", @rt.Float(1.25)),
    (@jni.JniType::double(), "doubleValue", @rt.Double(-3.5)),
  ]
  for (kind, name, expected) in primitive_cases {
    let field = fixture.static_field(name, kind)
    field.set(expected)
    require(same_primitive(expected, field.get()), "Primitive field conversion: " + name)
    let array = session.new_primitive_array(kind, 2)
    array.set_element(1, expected)
    require(same_primitive(expected, array.get_element(1)), "Primitive array conversion: " + name)
  }
  let mixed = fixture.static_method("mixed", @jni.JniMethod::new([
    @jni.JniType::boolean(), @jni.JniType::byte(), @jni.JniType::char(),
    @jni.JniType::short(), int_type, @jni.JniType::long(),
    @jni.JniType::float(), @jni.JniType::double(),
  ], return_type=@jni.JniType::long()))
  require(mixed.call([
    @rt.Boolean(true), @rt.Byte(-128), @rt.Char(0xd800), @rt.Short(-32768),
    @rt.Int(42), @rt.Long(9007199254740993L), @rt.Float(1.25), @rt.Double(-3.5),
  ]) is Long(9007199254740993L), "Primitive method arguments")
  let number = fixture.instance_field("number", int_type)
  require(integer(number.get(receiver=instance)) == 15, "Instance field get")
  number.set(@rt.Int(22), receiver=instance)
  require(integer(number.get(receiver=instance)) == 22, "Instance field set")
  let static_number = fixture.static_field("staticNumber", int_type)
  static_number.set(@rt.Int(31))
  require(integer(static_number.get()) == 31, "Static field set")
  let echo = fixture.instance_method("echo", @jni.JniMethod::new([string_type], return_type=string_type))
  require(object(echo.call([@rt.Object(Some(copied))], receiver=instance)).to_string_value() == original, "Java string echo")
  expect_invalid(() => { ignore(echo.call([@rt.Object(Some(instance))], receiver=instance)) }, "Nonassignable method argument")
  let label = fixture.instance_field("label", string_type)
  expect_invalid(() => { label.set(@rt.Object(Some(instance)), receiver=instance) }, "Nonassignable field value")
  let ints = session.new_primitive_array(int_type, 4)
  ints.set_element(0, @rt.Int(-7))
  ints.write_region(1, [@rt.Int(0), @rt.Int(42), @rt.Int(2147483647)])
  require(ints.array_length() == 4, "Primitive array length")
  let region = ints.read_region(0, 4)
  require(integer(region[0]) == -7 && integer(region[2]) == 42, "Primitive array region")
  require(integer(ints.get_element(3)) == 2147483647, "Primitive array element")
  let strings = session.find_class(@jni.JniClass::parse("java/lang/String")).new_object_array(3)
  strings.set_element(1, @rt.Object(Some(copied)))
  require(strings.array_length() == 3, "Object array length")
  require(strings.get_element(0) is Object(None), "Null object array element")
  require(object(strings.get_element(1)).is_same(copied), "Object identity in array")
  let initialized = session.find_class(@jni.JniClass::parse("java/lang/String")).new_object_array(2, initial=copied)
  require(object(initialized.get_element(0)).is_same(copied), "Object array nonnull initializer")
  require(object(initialized.get_element(1)).is_same(copied), "Object array initializer fills all elements")
  expect_invalid(() => { strings.set_element(0, @rt.Object(Some(instance))) }, "Nonassignable array element")
  expect_invalid(() => { ignore(ints.get_element(-1)) }, "Negative array index")
  expect_invalid(() => { ints.set_element(4, @rt.Int(1)) }, "Array end index")
  expect_invalid(() => { ignore(ints.read_region(3, 2)) }, "Array read region bounds")
  expect_invalid(() => { ints.write_region(3, [@rt.Int(1), @rt.Int(2)]) }, "Array write region bounds")
  let closed = session.new_string("closed")
  closed.close()
  let rejected = try {
    ignore(closed.to_string_value())
    false
  } catch {
    @rt.Closed => true
    _ => false
  }
  require(rejected, "Stale object handle rejected")
  let busy = try {
    session.close()
    false
  } catch {
    @rt.Busy => true
    _ => false
  }
  require(busy, "Managed session close must be Busy")
  let callback_signature = @jni.JniMethod::new([int_type], return_type=int_type)
  let inner = session.callback(callback_signature, (args) => {
    fixture_entry()
    @rt.Int(integer(args[0]) * 2)
  })
  fixture.static_field("inner", callback_type).set(@rt.Object(Some(inner.object())))
  let nested = fixture.static_method("nested", @jni.JniMethod::new([callback_type, int_type], return_type=int_type))
  let busy_method = fixture.static_method("closeBusySession", @jni.JniMethod::new([], return_type=int_type))
  let state = Ref(0)
  let counter = session.callback(callback_signature, (args) => {
    fixture_entry()
    state.val += integer(args[0])
    require(integer(busy_method.call([])) == 19, "Java nested session close must be Busy")
    nested.call([@rt.Object(Some(inner.object())), @rt.Int(state.val)])
  })
  fixture.static_field("counter", callback_type).set(@rt.Object(Some(counter.object())))
  let close_during = fixture.static_method("closeDuringCall", @jni.JniMethod::new([], return_type=int_type))
  let closing = session.callback(@jni.JniMethod::new([], return_type=int_type), (_) => {
    fixture_entry()
    close_during.call([])
  })
  fixture.static_field("closing", callback_type).set(@rt.Object(Some(closing.object())))
  let foreign = session.callback(callback_signature, (args) => {
    fixture_entry()
    args[0]
  })
  fixture.static_field("foreign", callback_type).set(@rt.Object(Some(foreign.object())))
  let throw_method = fixture.static_method("throwIdentity", @jni.JniMethod::new([]))
  let identity = object(fixture.static_field("identity", throwable_type).get())
  let throwing = session.callback(@jni.JniMethod::new([], return_type=int_type), (_) => {
    fixture_entry()
    try {
      ignore(throw_method.call([]))
      raise @rt.NativeFailure("Java exception did not propagate")
    } catch {
      @rt.JavaThrown(thrown) => {
        require(thrown.is_same(identity), "Throwable identity changed entering MoonBit")
        raise @rt.JavaThrown(thrown)
      }
      error => raise error
    }
  })
  fixture.static_field("throwing", callback_type).set(@rt.Object(Some(throwing.object())))
  let recovering = session.callback(@jni.JniMethod::new([], return_type=int_type), (_) => {
    fixture_entry()
    try {
      ignore(throw_method.call([]))
      raise @rt.NativeFailure("Expected recoverable Java exception")
    } catch {
      @rt.JavaThrown(_) => @rt.Int(73)
      error => raise error
    }
  })
  fixture.static_field("recovering", callback_type).set(@rt.Object(Some(recovering.object())))
  let invalid_throwable = session.callback(@jni.JniMethod::new([], return_type=int_type), (_) => {
    fixture_entry()
    raise @rt.JavaThrown(text)
  })
  fixture.static_field("invalidThrowable", callback_type).set(@rt.Object(Some(invalid_throwable.object())))
}

///|
fn after_foreign_close() -> Unit raise {
  let fixture = fixture_class(@rt.Session::current())
  require(integer(fixture.static_field("staticNumber", @jni.JniType::int()).get()) == 31, "Session survives callback tombstone drain")
}

///|
fn after_reopen() -> Unit raise @rt.RuntimeError {
  let session = @rt.Session::current()
  let old_session = match previous_session.val {
    Some(session) => session
    None => raise @rt.NativeFailure("Missing old session")
  }
  let old_object = match previous_object.val {
    Some(object) => object
    None => raise @rt.NativeFailure("Missing old object")
  }
  let session_rejected = try {
    ignore(old_session.new_string("stale"))
    false
  } catch {
    @rt.Closed => true
    _ => false
  }
  require(session_rejected, "Old session accepted operation after reopen")
  let object_rejected = try {
    ignore(old_object.to_string_value())
    false
  } catch {
    @rt.Closed => true
    _ => false
  }
  require(object_rejected, "Old object accepted operation after reopen")
  let fresh = session.new_string("new generation")
  require(fresh.to_string_value() == "new generation", "New session must remain usable")
  previous_session.val = None
  previous_object.val = None
}

///|
pub fn smoke_run(stage : Int) -> Int {
  try {
    match stage {
      0 => setup()
      1 => after_foreign_close()
      2 => after_reopen()
      _ => raise @rt.NativeFailure("Unknown fixture stage")
    }
    0
  } catch {
    error => {
      fixture_failure(error.to_string())
      1
    }
  }
}

///|
fn main {
  ()
}
