///|
extern "C" fn export_error(env : UInt64, throwable : UInt64) -> Unit = "ajni_android_export_error"

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
