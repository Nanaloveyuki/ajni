///|
#cfg(target="native")
extern "C" fn export_error(env : UInt64, throwable : UInt64) -> Unit = "ajni_android_export_error"

///|
#cfg(not(target="native"))
fn export_error(_env : UInt64, _throwable : UInt64) -> Unit {
  panic()
}

///|
fn return_export_error(env : UInt64, error : Error) -> UInt64 {
  match error {
    @runtime.JavaThrown(throwable) => {
      let local_ref = @runtime.unsafe_export_local(throwable, env) catch {
        _ => 0UL
      }
      export_error(env, local_ref)
    }
    _ => export_error(env, 0UL)
  }
  0UL
}

///|
/// Native instrumentation exercises the same callback registry used by applications.
pub fn create_int_callback(env : UInt64) -> UInt64 {
  try {
    let state = Ref(0)
    let signature = @ajni.JniMethod::new(
      [@ajni.JniType::int()],
      return_type=@ajni.JniType::int(),
    )
    let callback = @runtime.Session::current().callback(signature, args => {
      match args[0] {
        Int(value) => {
          state.val += value
          @runtime.Int(state.val)
        }
        _ => raise @runtime.InvalidArgument("Expected Java int")
      }
    })
    @runtime.unsafe_export_local(callback.object(), env)
  } catch {
    error => return_export_error(env, error)
  }
}

///|
pub fn handled_observer_error() -> Int32 {
  let handled = Ref(false)
  let token = install_event_handler(_event => {
    ignore(
      @runtime.Session::current().find_class(
        @ajni.JniClass::parse("dev/nanaloveyuki/ajni/MissingObserverClass"),
      ),
    ) catch {
      @runtime.JavaThrown(_) => handled.val = true
      _ => ()
    }
  })
  emit_for_test(21, 0, 0)
  remove_event_handler(token)
  if handled.val {
    1
  } else {
    0
  }
}

///|
let context_handles : Ref[(@runtime.JavaObject, @runtime.JavaObject)?] = Ref(
  None,
)

///|
let context_subscription : Ref[Int32?] = Ref(None)

///|
let context_resume_count : Ref[Int32] = Ref(0)

///|
fn require_context_contract(
  condition : Bool,
  message : String,
) -> Unit raise @runtime.RuntimeError {
  if !condition {
    raise @runtime.NativeFailure(message)
  }
}

///|
fn retained_context() -> @runtime.JavaObject raise @runtime.RuntimeError {
  match context_handles.val {
    Some((_, context)) => context
    None =>
      raise @runtime.NativeFailure("Context acceptance handles are missing")
  }
}

///|
fn context_package_name(
  context : @runtime.JavaObject,
) -> @runtime.JavaObject raise @runtime.RuntimeError {
  let class = @runtime.Session::current().find_class(
    try! @ajni.JniClass::parse("android/content/Context"),
  )
  errdefer (class.object().close() catch { _ => () })
  let text = {
    let get_name = class.instance_method(
      "getPackageName",
      try! @ajni.JniMethod::new(
        [],
        return_type=@ajni.JniType::object(
          try! @ajni.JniClass::parse("java/lang/String"),
        ),
      ),
    )
    match get_name.call([], receiver=context) {
      Object(Some(text)) => text
      _ => raise @runtime.NativeFailure("Context.getPackageName returned null")
    }
  }
  errdefer (text.close() catch { _ => () })
  class.object().close()
  text
}

///|
fn export_temporary_context(
  env : UInt64,
  context : @runtime.JavaObject,
) -> UInt64 raise @runtime.RuntimeError {
  defer (context.close() catch { _ => () })
  @runtime.unsafe_export_local(context, env)
}

///|
/// Stages retain checked handles across separate Android lifecycle entries.
pub fn application_context_for_test(
  env : UInt64,
  stage : Int32,
  before_init_rejected : Bool,
) -> UInt64 {
  try {
    match stage {
      0 => {
        require_context_contract(
          before_init_rejected, "Package initialization must reject Context acquisition with NotInitialized",
        )
        require_context_contract(
          context_handles.val is None,
          "Context acceptance is already active",
        )
        let first = application_context()
        let second = application_context()
        require_context_contract(
          first.is_same(second),
          "Independent Context handles must have the same Java identity",
        )
        context_handles.val = Some((first, second))
        context_resume_count.val = 0
        context_subscription.val = Some(
          install_event_handler(event => {
            if event == Lifecycle(Resumed) {
              context_resume_count.val += 1
            }
          }),
        )
        @runtime.unsafe_export_local(first, env)
      }
      1 => {
        let (first, second) = match context_handles.val {
          Some(handles) => handles
          None =>
            raise @runtime.NativeFailure(
              "Context acceptance handles are missing",
            )
        }
        let shared = first
        first.close()
        let alias_closed = try {
          ignore(shared.is_same(second))
          false
        } catch {
          @runtime.Closed => true
          error => raise error
        }
        require_context_contract(
          alias_closed, "Aliases must share Context handle close state",
        )
        context_package_name(second).close()
        let fresh = application_context()
        require_context_contract(
          second.is_same(fresh),
          "Closing one Context handle must preserve reacquisition",
        )
        export_temporary_context(env, fresh)
      }
      2 => export_temporary_context(env, application_context())
      3 => {
        let old = retained_context()
        let fresh = application_context()
        let stale_closed = try {
          ignore(old.is_same(fresh))
          false
        } catch {
          @runtime.Closed => true
          error => raise error
        }
        require_context_contract(
          stale_closed, "Reopening a session must reject stale Context handles with Closed",
        )
        context_handles.val = Some((fresh, fresh))
        @runtime.unsafe_export_local(fresh, env)
      }
      4 => {
        if context_subscription.val is Some(token) {
          remove_event_handler(token)
          context_subscription.val = None
        }
        if context_handles.val is Some((first, second)) {
          first.close() catch {
            _ => ()
          }
          second.close() catch {
            _ => ()
          }
          context_handles.val = None
        }
        export_temporary_context(env, application_context())
      }
      5 => {
        require_context_contract(
          context_resume_count.val == 2,
          "The original lifecycle subscription must survive feature reinitialization",
        )
        let fresh = application_context()
        require_context_contract(
          retained_context().is_same(fresh),
          "Old Context handles must survive feature reinitialization",
        )
        export_temporary_context(env, fresh)
      }
      _ => raise @runtime.InvalidArgument("Unknown Context acceptance stage")
    }
  } catch {
    error => return_export_error(env, error)
  }
}

///|
pub fn context_package_name_for_test(env : UInt64) -> UInt64 {
  export_temporary_context(env, context_package_name(retained_context())) catch {
    error => return_export_error(env, error)
  }
}
