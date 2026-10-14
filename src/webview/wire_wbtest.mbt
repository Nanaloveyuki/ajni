///|
test "page messages decode structured trusted-origin fields" {
  let events : Array[Event] = []
  let id = install_event_handler(fn(event) { events.push(event) })
  dispatch_from_android(
    37, 9L, b"body=aGVsbG8&origin=aHR0cHM6Ly9vcmJpdC50ZXN0&main_frame=MQ", b"",
  )
  match events[0] {
    { handle: 9L, kind: PageMessage(body, origin, true), } => {
      assert_eq(body, "hello")
      assert_eq(origin, "https://orbit.test")
    }
    _ => abort("expected a structured page message")
  }
  remove_event_handler(id)
}

///|
test "page messages accept the Android boolean spelling" {
  let events : Array[Event] = []
  let id = install_event_handler(fn(event) { events.push(event) })
  dispatch_from_android(
    37,
    10L,
    @ajni.encode_utf8(
      encode_fields([
        ("body", "hello"),
        ("origin", "https://app.orbit.invalid"),
        ("main_frame", "true"),
      ]),
    ),
    b"",
  )
  match events[0] {
    { kind: PageMessage(_, _, true), .. } => ()
    _ => abort("expected true Android main-frame spelling")
  }
  remove_event_handler(id)
}

///|
test "asset requests and operation failures retain correlation identifiers" {
  let events : Array[Event] = []
  let id = install_event_handler(fn(event) { events.push(event) })
  dispatch_from_android(
    38,
    11L,
    @ajni.encode_utf8(
      encode_fields([("request_id", "asset-2"), ("path", "ui/main.js")]),
    ),
    b"",
  )
  dispatch_from_android(
    39,
    11L,
    @ajni.encode_utf8("timed out"),
    @ajni.encode_utf8("load-4"),
  )
  match events[0] {
    { handle: 11L, kind: AssetRequest({ id: "asset-2", path: "ui/main.js", }), } =>
      ()
    _ => abort("expected an asset request")
  }
  match events[1] {
    { handle: 11L, kind: OperationFailed("load-4", "timed out"), } => ()
    _ => abort("expected a correlated operation failure")
  }
  remove_event_handler(id)
}

///|
test "encoded asset response preserves binary body and response metadata" {
  let wire = encode_asset_response("asset-4", {
    status: 200,
    mime_type: "text/plain",
    encoding: "utf-8",
    headers: [{ name: "Cache-Control", value: "no-store", }],
    body: b"\x00\xff",
  })
  let fields = decode_fields(wire).unwrap_or(Map([]))
  assert_eq(fields.get("request_id").unwrap_or(""), "asset-4")
  assert_eq(fields.get("status").unwrap_or(""), "200")
  assert_eq(fields.get("mime").unwrap_or(""), "text/plain")
  assert_eq(fields.get("encoding").unwrap_or(""), "utf-8")
  assert_eq(fields.get("body").unwrap_or(""), "AP8")
  assert_eq(
    fields.get("headers"),
    Some("bmFtZT1RMkZqYUdVdFEyOXVkSEp2YkEmdmFsdWU9Ym04dGMzUnZjbVU"),
  )
}

///|
test "create wire payload preserves trusted origin scripts and quotas" {
  let wire = encode_create_options(
    "https://app.orbit.invalid",
    "html",
    "<main>orbit</main>",
    "https://app.orbit.invalid/index.html",
    ["window.orbitReady = true"],
    WebViewResourceLimits::new(
      max_page_message_bytes=4096,
      max_pending_asset_requests=4,
      max_asset_response_bytes=8192,
      asset_response_timeout_millis=250,
    ),
  )
  let fields = decode_fields(wire).unwrap_or(Map([]))
  assert_eq(fields.get("origin").unwrap_or(""), "https://app.orbit.invalid")
  assert_eq(fields.get("initial_kind").unwrap_or(""), "html")
  assert_eq(fields.get("initial").unwrap_or(""), "<main>orbit</main>")
  assert_eq(
    fields.get("base_url").unwrap_or(""),
    "https://app.orbit.invalid/index.html",
  )
  assert_eq(
    fields.get("scripts").unwrap_or(""),
    "d2luZG93Lm9yYml0UmVhZHkgPSB0cnVl",
  )
  assert_eq(fields.get("max_page_message_bytes").unwrap_or(""), "4096")
  assert_eq(fields.get("max_pending_asset_requests").unwrap_or(""), "4")
  assert_eq(fields.get("max_asset_response_bytes").unwrap_or(""), "8192")
  assert_eq(fields.get("asset_response_timeout_millis").unwrap_or(""), "250")
}

///|
test "malformed webview fields are dropped instead of defaulting" {
  let events : Array[Event] = []
  let id = install_event_handler(fn(event) { events.push(event) })
  dispatch_from_android(
    37,
    12L,
    @ajni.encode_utf8("body=aGVsbG8&body=d29ybGQ&origin=aHR0cHM6Ly9hcHA"),
    b"",
  )
  assert_eq(events.length(), 0)
  remove_event_handler(id)
}

///|
test "invalid base64 and missing fields are rejected" {
  let events : Array[Event] = []
  let id = install_event_handler(fn(event) { events.push(event) })
  dispatch_from_android(
    38,
    13L,
    @ajni.encode_utf8("request_id=%%%25&path="),
    b"",
  )
  dispatch_from_android(37, 13L, @ajni.encode_utf8("body=aGVsbG8"), b"")
  assert_eq(events.length(), 0)
  remove_event_handler(id)
}

///|
test "literal canonical fields preserve scalar text and nested binary base64" {
  for
    fixture in [
      ("body=", ""),
      ("body=AA", "\u0000"),
      ("body=5Lit", "中"),
      ("body=8J-agA", "\u{1F680}"),
      ("body=77u_", "\uFEFF"),
      ("body=QVA4", "AP8"),
    ] {
    match decode_fields(fixture.0) {
      Some(fields) => assert_eq(fields.get("body"), Some(fixture.1))
      None => abort("canonical field was rejected")
    }
  }
}

///|
test "literal page messages reject invalid text and incomplete fields" {
  for
    wire in [
      "body=aGVsbG8", "body=&body=AA&origin=&main_frame=MQ", "body=_w&origin=&main_frame=MQ",
      "body=AA&&origin=&main_frame=MQ", "body=AA==&origin=&main_frame=MQ",
    ] {
    assert_eq(decode_page_message(@ajni.encode_utf8(wire)), None)
  }
}
