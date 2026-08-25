///|
fn android_positive_mod(value : Int, modulus : Int) -> Int {
  let remainder = value % modulus
  if remainder < 0 {
    remainder + modulus
  } else {
    remainder
  }
}

///|
fn expected_android_event(input : (Int, Int, Int)) -> (Int, AndroidEvent) {
  match android_positive_mod(input.0, 11) {
    0 => (1, Lifecycle(Created))
    1 => (2, Lifecycle(Started))
    2 => (3, Lifecycle(Resumed))
    3 => (4, Lifecycle(Paused))
    4 => (5, Lifecycle(Stopped))
    5 => (6, Lifecycle(Destroyed))
    6 => (10, SurfaceCreated(input.1, input.2))
    7 => (11, SurfaceChanged(input.1, input.2))
    8 => (12, SurfaceDestroyed)
    9 => (20, WorkerAttached)
    _ => (21, UiTask)
  }
}

///|
test "quickcheck: Android event codes map to the documented event model" {
  @quickcheck.check(
    (input : (Int, Int, Int)) => {
      let (kind, expected) = expected_android_event(input)
      let events : Array[AndroidEvent] = []
      let id = install_event_handler(fn(event) { events.push(event) })
      dispatch_from_android(kind, input.1, input.2)
      remove_event_handler(id)
      events == [expected]
    },
    count=750,
    max_size=128,
    max_shrinks=100,
    seed=2026082521,
  )
}

///|
test "quickcheck: unknown Android event codes are ignored" {
  @quickcheck.check(
    (input : (Int, Int, Int)) => {
      let events : Array[AndroidEvent] = []
      let id = install_event_handler(fn(event) { events.push(event) })
      dispatch_from_android(
        100 + android_positive_mod(input.0, 1000),
        input.1,
        input.2,
      )
      remove_event_handler(id)
      events.length() == 0
    },
    count=500,
    max_size=128,
    max_shrinks=100,
    seed=2026082522,
  )
}
