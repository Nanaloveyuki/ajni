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
