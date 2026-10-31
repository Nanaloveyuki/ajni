///|
#external
type HostContext

///|
suberror ProbeError {
  NegativeDelta
}

///|
let accumulated : Ref[Int32] = Ref(0)

///|
let managed_entries : Ref[Int32] = Ref(0)

///|
pub fn reset() -> Unit {
  accumulated.val = 0
  managed_entries.val = 0
}

///|
pub fn total() -> Int32 {
  accumulated.val
}

///|
pub fn entries() -> Int32 {
  managed_entries.val
}

///|
fn add_delta(delta : Int32) -> Int32 raise ProbeError {
  if delta < 0 {
    raise NegativeDelta
  }
  accumulated.val += delta
  accumulated.val
}

///|
pub fn round_trip(
  context : HostContext,
  delta : Int32,
  text : String,
  bytes : Bytes,
  callback : FuncRef[(HostContext, Int32, String, Bytes) -> Int32],
) -> Int32 {
  managed_entries.val += 1
  let value = add_delta(delta) catch { NegativeDelta => return -2147483648 }
  callback(context, value, text, bytes)
}

///|
pub fn worker(sequence : Int32) -> Int32 {
  managed_entries.val += 1
  accumulated.val += sequence
  accumulated.val
}

///|
fn main {
  ()
}
