///|
test "lifecycle and surface event mapping" {
  let events : Array[@android.AndroidEvent] = []
  let id = @android.install_event_handler(fn(event) { events.push(event) })
  @android.emit_for_test(1, 0, 0)
  @android.emit_for_test(10, 1080, 1920)
  @android.emit_for_test(12, 0, 0)
  debug_inspect(
    events,
    content="[Lifecycle(Created), SurfaceCreated(1080, 1920), SurfaceDestroyed]",
  )
  @android.remove_event_handler(id)
}

///|
test "handler mutation preserves current snapshot and refreshes nested dispatch" {
  let visits : Map[(Int32, Int32), Int32] = Map([])
  let b = Ref(0)
  let c = Ref(0)
  let a = @android.install_event_handler(fn(event) {
    if event is SurfaceCreated(phase, _) {
      visits.set((1, phase), visits.get((1, phase)).unwrap_or(0) + 1)
      if phase == 1 {
        @android.remove_event_handler(b.val)
        c.val = @android.install_event_handler(fn(event) {
          if event is SurfaceCreated(phase, _) {
            visits.set((3, phase), visits.get((3, phase)).unwrap_or(0) + 1)
          }
        })
        @android.dispatch_from_android(10, 2, 0)
      }
    }
  })
  b.val = @android.install_event_handler(fn(event) {
    if event is SurfaceCreated(phase, _) {
      visits.set((2, phase), visits.get((2, phase)).unwrap_or(0) + 1)
    }
  })
  @android.dispatch_from_android(10, 1, 0)
  @android.dispatch_from_android(10, 3, 0)
  @android.remove_event_handler(a)
  @android.remove_event_handler(b.val)
  @android.remove_event_handler(c.val)
  for phase in [1, 2, 3] {
    assert_eq(visits.get((1, phase)), Some(1))
  }
  assert_eq(visits.get((2, 1)), Some(1))
  assert_eq(visits.get((3, 1)), None)
  for phase in [2, 3] {
    assert_eq(visits.get((2, phase)), None)
    assert_eq(visits.get((3, phase)), Some(1))
  }
}
