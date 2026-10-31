///|
/// Stable Android C entry point. The JNI bridge links this package in a
/// production Android build and calls the exported `ajni_dispatch_event`.
pub fn dispatch(kind : Int32, first : Int32, second : Int32) -> Unit {
  @android.dispatch_from_android(kind, first, second)
}

///|
pub fn dispatch_webview(
  kind : Int32,
  handle : Int64,
  payload : Bytes,
  detail : Bytes,
) -> Unit {
  @webview.dispatch_from_android(kind, handle, payload, detail)
}

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
    let signature = @jni.JniMethod::new(
      [@jni.JniType::int()],
      return_type=@jni.JniType::int(),
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
  let token = @android.install_event_handler(_event => {
    ignore(
      @runtime.Session::current().find_class(
        @jni.JniClass::parse("dev/nanaloveyuki/ajni/MissingObserverClass"),
      ),
    ) catch {
      @runtime.JavaThrown(_) => handled.val = true
      _ => ()
    }
  })
  @android.emit_for_test(21, 0, 0)
  @android.remove_event_handler(token)
  if handled.val {
    1
  } else {
    0
  }
}

///|
fn main {
  ()
}
