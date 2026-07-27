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

///|
test "WebView callbacks preserve the handle and UTF-8 payload" {
  let events : Array[AndroidEvent] = []
  let id = install_event_handler(fn(event) { events.push(event) })
  dispatch_webview_from_android(
    33,
    42L,
    encode_utf8("JNI \u4e2d\u6587 \u{1F680}"),
  )
  match events[0] {
    WebView(handle, TitleChanged(title)) => {
      assert_eq(handle, 42L)
      assert_eq(title, "JNI \u4e2d\u6587 \u{1F680}")
    }
    _ => abort("expected a WebView title callback")
  }
  remove_event_handler(id)
}
