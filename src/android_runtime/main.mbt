///|
/// Stable Android C entry point. The JNI bridge links this package in a
/// production Android build and calls the exported `ajni_dispatch_event`.
pub fn dispatch(kind : Int, first : Int, second : Int) -> Unit {
  @ajni.dispatch_from_android(kind, first, second)
}

///|
fn main {
  ()
}
