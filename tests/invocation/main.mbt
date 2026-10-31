///|
extern "C" fn fixture_native_address() -> UInt64 = "ajni_invocation_fixture_native_address"

///|
extern "C" fn fixture_delete_local(env : UInt64, reference : UInt64) -> Unit = "ajni_invocation_fixture_delete_local"

///|
extern "C" fn fixture_failure() -> Unit = "ajni_invocation_fixture_failure"

///|
fn require(condition : Bool, message : String) -> Unit raise @rt.RuntimeError {
  if !condition {
    raise @rt.NativeFailure(message)
  }
}

///|
fn integer(value : @rt.JavaValue) -> Int32 raise @rt.RuntimeError {
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
fn options(classes : String) -> Array[String] {
  ["-Xcheck:jni", "-Djava.class.path=\{classes}"]
}

///|
fn import_java_class(
  session : @rt.Session,
  class_object : @rt.JavaObject,
) -> @rt.JavaClass raise @rt.RuntimeError {
  let env = @raw.raw_env(session)
  let local_ref = @raw.export_local(class_object, env)
  let imported = @raw.import_class(session, env, local_ref)
  fixture_delete_local(env, local_ref)
  imported
}

///|
fn imported_loader_regressions(
  session : @rt.Session,
  fixture : @rt.JavaClass,
) -> Unit raise {
  let int_type = @jni.JniType::int()
  let class_type = @jni.JniType::object(@jni.JniClass::parse("java/lang/Class"))
  let object_type = @jni.JniType::object(
    @jni.JniClass::parse("java/lang/Object"),
  )
  let callback_type = @jni.JniType::object(
    @jni.JniClass::parse("dev/nanaloveyuki/ajni/AjniCallback"),
  )
  let parameter_type = @jni.JniType::object(
    @jni.JniClass::parse("dev/nanaloveyuki/ajni/invocation/Fixture$PluginParam"),
  )
  let new_plugin = fixture.static_method(
    "newPluginClass",
    @jni.JniMethod::new([], return_type=class_type),
  )
  let parameter = fixture.static_method(
    "pluginParameter",
    @jni.JniMethod::new([class_type], return_type=object_type),
  )
  let arm_load = fixture.static_method(
    "armParameterLoad",
    @jni.JniMethod::new([class_type, callback_type]),
  )
  let accept_signature = @jni.JniMethod::new(
    [parameter_type],
    return_type=int_type,
  )
  let isolated_class_object = object(new_plugin.call([]))
  let isolated_class = import_java_class(session, isolated_class_object)
  let isolated_parameter = object(
    parameter.call([@rt.Object(Some(isolated_class_object))]),
  )
  let accept = isolated_class.static_method("accept", accept_signature)
  require(
    integer(accept.call([@rt.Object(Some(isolated_parameter))])) == 42,
    "Imported class descriptor uses its defining loader",
  )
  let closing_class_object = object(new_plugin.call([]))
  let closing_class = import_java_class(session, closing_class_object)
  let closes = Ref(0)
  let close_during_load = session.callback(@jni.JniMethod::new([]), _ => {
    closes.val += 1
    closing_class.object().close()
    @rt.Void
  })
  ignore(
    arm_load.call([
      @rt.Object(Some(closing_class_object)),
      @rt.Object(Some(close_during_load.object())),
    ]),
  )
  let retained_accept = closing_class.static_method("accept", accept_signature)
  require(
    closes.val == 1,
    "Descriptor class resolution synchronously reentered MoonBit",
  )
  let closed = try {
    ignore(closing_class.static_method("accept", accept_signature))
    false
  } catch {
    @rt.Closed => true
    _ => false
  }
  require(closed, "Reentrant close invalidates the original declaring wrapper")
  let retained_parameter = object(
    parameter.call([@rt.Object(Some(closing_class_object))]),
  )
  require(
    integer(retained_accept.call([@rt.Object(Some(retained_parameter))])) == 42,
    "Suspended lookup retains its independent declaring class",
  )
  close_during_load.close()
  isolated_class.object().close()
  isolated_class_object.close()
  closing_class_object.close()
}

///|
fn success(path : String, classes : String) -> Unit raise {
  let session = @invocation.create(path, options(classes))
  let fixture = session.find_class(
    @jni.JniClass::parse("dev/nanaloveyuki/ajni/invocation/Fixture"),
  )
  let int_type = @jni.JniType::int()
  let string_type = @jni.JniType::object(
    @jni.JniClass::parse("java/lang/String"),
  )
  let callback_type = @jni.JniType::object(
    @jni.JniClass::parse("dev/nanaloveyuki/ajni/AjniCallback"),
  )
  let instance = fixture.lookup_constructor([int_type]).new([@rt.Int(12)])
  let add = fixture.instance_method(
    "add",
    @jni.JniMethod::new([int_type], return_type=int_type),
  )
  require(
    integer(add.call([@rt.Int(30)], receiver=instance)) == 42,
    "Native-host Java invocation",
  )
  require(
    integer(fixture.instance_field("number", int_type).get(receiver=instance)) ==
    42,
    "Java object state",
  )
  let text = session.new_string("native\u0000host\u4e2d\u{1F600}")
  require(
    text.to_string_value() == "native\u0000host\u4e2d\u{1F600}",
    "Native-host UTF-16 round trip",
  )
  let ints = session.new_primitive_array(int_type, 3)
  ints.write_region(0, [@rt.Int(-1), @rt.Int(0), @rt.Int(42)])
  require(integer(ints.get_element(2)) == 42, "Native-host array region")
  let property = session
    .find_class(@jni.JniClass::parse("java/lang/System"))
    .static_method(
      "getProperty",
      @jni.JniMethod::new([string_type], return_type=string_type),
    )
  require(
    object(
      property.call([@rt.Object(Some(session.new_string("java.class.path")))]),
    ).to_string_value() ==
    classes,
    "JVM options honored",
  )
  let callback_signature = @jni.JniMethod::new([int_type], return_type=int_type)
  let invoke = fixture.static_method(
    "invoke",
    @jni.JniMethod::new([callback_type, int_type], return_type=int_type),
  )
  let total = Ref(0)
  let inner = session.callback(callback_signature, args => {
    total.val += integer(args[0])
    @rt.Int(total.val)
  })
  let nested = session.callback(callback_signature, args => {
    invoke.call([@rt.Object(Some(inner.object())), args[0]])
  })
  require(
    integer(invoke.call([@rt.Object(Some(nested.object())), @rt.Int(7)])) == 7,
    "Native-host nested callback",
  )
  require(
    integer(invoke.call([@rt.Object(Some(nested.object())), @rt.Int(5)])) == 12,
    "Captured callback state",
  )
  let foreign = fixture.static_method(
    "invokeFromWorker",
    @jni.JniMethod::new([callback_type], return_type=int_type),
  )
  require(
    integer(foreign.call([@rt.Object(Some(inner.object()))])) == 1,
    "Foreign-thread callback rejected",
  )
  require(total.val == 12, "Foreign callback did not enter MoonBit")
  let throw_identity = fixture.static_method(
    "throwIdentity",
    @jni.JniMethod::new([]),
  )
  let throwing = session.callback(
    @jni.JniMethod::new([], return_type=int_type),
    _ => {
      ignore(throw_identity.call([]))
      raise @rt.NativeFailure("Expected Java exception")
    },
  )
  let verify_identity = fixture.static_method(
    "verifyIdentity",
    @jni.JniMethod::new([callback_type], return_type=int_type),
  )
  require(
    integer(verify_identity.call([@rt.Object(Some(throwing.object()))])) == 1,
    "Original Throwable identity survives callback",
  )
  let busy = session.callback(@jni.JniMethod::new([], return_type=int_type), _ => {
    let rejected = try {
      session.close()
      false
    } catch {
      @rt.Busy => true
      _ => false
    }
    require(rejected, "Session close within callback must be Busy")
    @rt.Int(1)
  })
  let busy_invoke = busy.object()
  let callback_class = session.find_class(
    @jni.JniClass::parse("dev/nanaloveyuki/ajni/AjniCallback"),
  )
  let object_type = @jni.JniType::object(
    @jni.JniClass::parse("java/lang/Object"),
  )
  let object_array = session
    .find_class(@jni.JniClass::parse("java/lang/Object"))
    .new_object_array(0)
  let direct_invoke = callback_class.instance_method(
    "invoke",
    @jni.JniMethod::new([object_type.array()], return_type=object_type),
  )
  let boxed = object(
    direct_invoke.call([@rt.Object(Some(object_array))], receiver=busy_invoke),
  )
  require(
    integer(
      session
      .find_class(@jni.JniClass::parse("java/lang/Integer"))
      .instance_method(
        "intValue",
        @jni.JniMethod::new([], return_type=int_type),
      )
      .call([], receiver=boxed),
    ) ==
    1,
    "Busy callback completed",
  )
  let env = @raw.raw_env(session)
  require(@raw.raw_vm(session) != 0 && env != 0, "Raw VM and owner Env")
  let local_ref = @raw.export_local(text, env)
  let imported = @raw.import_object(session, env, local_ref)
  fixture_delete_local(env, local_ref)
  require(imported.is_same(text), "Unsafe import promotes local reference")
  let raw_class = @raw.export_local(fixture.object(), env)
  let imported_class = @raw.import_class(session, env, raw_class)
  fixture_delete_local(env, raw_class)
  @raw.register_native(
    imported_class,
    "nativeAdd",
    "(I)I",
    fixture_native_address(),
  )
  require(
    integer(
      fixture.static_method("nativeAdd", callback_signature).call([@rt.Int(39)]),
    ) ==
    42,
    "Raw JNI native registration",
  )
  imported_loader_regressions(session, fixture)
  let repeated = try {
    ignore(@invocation.create(path, options(classes)))
    false
  } catch {
    @invocation.AlreadyAttempted => true
    _ => false
  }
  require(repeated, "Only one JVM creation attempt admitted")
  inner.close()
  nested.close()
  throwing.close()
  busy.close()
  session.close()
  let stale = try {
    ignore(text.to_string_value())
    false
  } catch {
    @rt.Closed => true
    _ => false
  }
  require(stale, "Session close invalidates handles")
  println("AJNI_INVOCATION_OK")
}

///|
fn failure_case(mode : String, path : String, classes : String) -> Unit raise {
  let failed = try {
    let vm_options = options(classes)
    if mode == "create-failure" {
      vm_options.push("-XX:AjniDefinitelyNotARealOption=true")
    }
    ignore(@invocation.create(path, vm_options))
    false
  } catch {
    @invocation.LoadFailed(_) => mode == "bad-path"
    @invocation.ResolveFailed(_) => mode == "missing-symbol"
    @invocation.CreateFailed(status) => mode == "create-failure" && status != 0
    _ => false
  }
  require(failed, "Invocation failure mode did not report its expected error")
  if mode == "create-failure" {
    let repeated = try {
      ignore(@invocation.create(path, options(classes)))
      false
    } catch {
      @invocation.AlreadyAttempted => true
      _ => false
    }
    require(repeated, "Failed JNI creation must consume admission")
  }
  match mode {
    "bad-path" => println("AJNI_INVOCATION_BAD_PATH_OK")
    "missing-symbol" => println("AJNI_INVOCATION_MISSING_SYMBOL_OK")
    "create-failure" => println("AJNI_INVOCATION_CREATE_FAILURE_OK")
    _ => raise @rt.NativeFailure("Unknown invocation failure mode")
  }
}

///|
fn main {
  try {
    let args = @env.args()
    require(
      args.length() == 4,
      "Expected MODE FULL_LIBRARY_PATH CLASSES_DIRECTORY",
    )
    if args[1] == "success" {
      success(args[2], args[3])
    } else {
      failure_case(args[1], args[2], args[3])
    }
  } catch {
    error => {
      println("AJNI_INVOCATION_FAILED: \{error}")
      fixture_failure()
    }
  }
}
