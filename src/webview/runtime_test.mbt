///|
test "callbacks preserve the handle and UTF-8 payload" {
  let events : Array[@webview.Event] = []
  let id = @webview.install_event_handler(fn(event) { events.push(event) })
  @webview.dispatch_from_android(
    33,
    42L,
    @ajni.encode_utf8("JNI \u4e2d\u6587 \u{1F680}"),
    b"",
  )
  match events[0] {
    { handle: 42L, kind: TitleChanged(title), } =>
      assert_eq(title, "JNI \u4e2d\u6587 \u{1F680}")
    _ => abort("expected a WebView title callback")
  }
  @webview.remove_event_handler(id)
}

///|
test "script results preserve their request identifier" {
  let events : Array[@webview.Event] = []
  let id = @webview.install_event_handler(fn(event) { events.push(event) })
  @webview.dispatch_from_android(34, 7L, b"null", b"eval-9")
  match events[0] {
    { handle: 7L, kind: ScriptResult(request_id, result), } => {
      assert_eq(request_id, "eval-9")
      assert_eq(result, "null")
    }
    _ => abort("expected a correlated script result")
  }
  @webview.remove_event_handler(id)
}

///|
test "correlated callbacks drop either malformed side and preserve BOM" {
  let events : Array[@webview.Event] = []
  let id = @webview.install_event_handler(fn(event) { events.push(event) })
  for kind in [34, 39] {
    @webview.dispatch_from_android(kind, 7L, b"\xff", b"id")
    @webview.dispatch_from_android(kind, 7L, b"text", b"\xff")
    @webview.dispatch_from_android(kind, 7L, b"\xff", b"\xff")
  }
  @webview.dispatch_from_android(
    39, 7L, b"\xef\xbb\xbfmessage\x00", b"operation",
  )
  @webview.dispatch_from_android(34, 7L, b"\xef\xbb\xbfnull", b"eval")
  @webview.remove_event_handler(id)
  assert_eq(events.length(), 2)
  assert_eq(events[0].handle, 7L)
  assert_eq(events[1].handle, 7L)
  match events[0].kind {
    OperationFailed(id, message) => {
      assert_eq(id, "operation")
      assert_eq(message, "\uFEFFmessage\u0000")
    }
    _ => abort("expected correlated operation failure")
  }
  match events[1].kind {
    ScriptResult(id, result) => {
      assert_eq(id, "eval")
      assert_eq(result, "\uFEFFnull")
    }
    _ => abort("expected correlated script result")
  }
}

///|
test "handler mutation preserves current snapshot and refreshes nested dispatch" {
  let visits : Map[(Int32, Int64), Int32] = Map([])
  let b = Ref(0)
  let c = Ref(0)
  let a = @webview.install_event_handler(fn(event) {
    let phase = event.handle
    visits.set((1, phase), visits.get((1, phase)).unwrap_or(0) + 1)
    if phase == 1L {
      @webview.remove_event_handler(b.val)
      c.val = @webview.install_event_handler(fn(event) {
        let phase = event.handle
        visits.set((3, phase), visits.get((3, phase)).unwrap_or(0) + 1)
      })
      @webview.dispatch_from_android(36, 2L, b"", b"")
    }
  })
  b.val = @webview.install_event_handler(fn(event) {
    let phase = event.handle
    visits.set((2, phase), visits.get((2, phase)).unwrap_or(0) + 1)
  })
  @webview.dispatch_from_android(36, 1L, b"", b"")
  @webview.dispatch_from_android(36, 3L, b"", b"")
  @webview.remove_event_handler(a)
  @webview.remove_event_handler(b.val)
  @webview.remove_event_handler(c.val)
  for phase in [1L, 2L, 3L] {
    assert_eq(visits.get((1, phase)), Some(1))
  }
  assert_eq(visits.get((2, 1L)), Some(1))
  assert_eq(visits.get((3, 1L)), None)
  for phase in [2L, 3L] {
    assert_eq(visits.get((2, phase)), None)
    assert_eq(visits.get((3, phase)), Some(1))
  }
}
