///|
test "callbacks preserve the handle and UTF-8 payload" {
  let events : Array[Event] = []
  let id = install_event_handler(fn(event) { events.push(event) })
  dispatch_from_android(
    33,
    42L,
    @ajni.encode_utf8("JNI \u4e2d\u6587 \u{1F680}"),
    b"",
  )
  match events[0] {
    { handle: 42L, kind: TitleChanged(title) } =>
      assert_eq(title, "JNI \u4e2d\u6587 \u{1F680}")
    _ => abort("expected a WebView title callback")
  }
  remove_event_handler(id)
}

///|
test "script results preserve their request identifier" {
  let events : Array[Event] = []
  let id = install_event_handler(fn(event) { events.push(event) })
  dispatch_from_android(
    34,
    7L,
    @ajni.encode_utf8("null"),
    @ajni.encode_utf8("eval-9"),
  )
  match events[0] {
    { handle: 7L, kind: ScriptResult(request_id, result) } => {
      assert_eq(request_id, "eval-9")
      assert_eq(result, "null")
    }
    _ => abort("expected a correlated script result")
  }
  remove_event_handler(id)
}
