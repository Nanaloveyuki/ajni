///|
test "lifecycle and surface event mapping" {
  let events : Array[AndroidEvent] = []
  let id = install_event_handler(fn(event) { events.push(event) })
  emit_for_test(1, 0, 0)
  emit_for_test(10, 1080, 1920)
  emit_for_test(12, 0, 0)
  debug_inspect(
    events,
    content="[Lifecycle(Created), SurfaceCreated(1080, 1920), SurfaceDestroyed]",
  )
  remove_event_handler(id)
}

///|
test "local frame normalises capacity" {
  debug_inspect(LocalFrame::new(capacity=0), content="{ capacity: 1 }")
}
