///|
test "page messages decode structured trusted-origin fields" {
  let events : Array[Event] = []
  let id = install_event_handler(fn(event) { events.push(event) })
  dispatch_from_android(
    37,
    9L,
    @ajni.encode_utf8(
      encode_fields([
        ("body", "hello from page"),
        ("origin", "https://app.orbit.invalid"),
        ("main_frame", "1"),
      ]),
    ),
    b"",
  )
  match events[0] {
    { handle: 9L, kind: PageMessage(body, origin, true) } => {
      assert_eq(body, "hello from page")
      assert_eq(origin, "https://app.orbit.invalid")
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
    { handle: 11L, kind: AssetRequest({ id: "asset-2", path: "ui/main.js" }) } =>
      ()
    _ => abort("expected an asset request")
  }
  match events[1] {
    { handle: 11L, kind: OperationFailed("load-4", "timed out") } => ()
    _ => abort("expected a correlated operation failure")
  }
  remove_event_handler(id)
}

///|
test "resource limits default and encoded asset response are bounded fields" {
  let limits = WebViewResourceLimits::new()
  assert_eq(limits.max_page_message_bytes, 1048576)
  assert_eq(limits.max_pending_asset_requests, 32)
  assert_eq(limits.max_asset_response_bytes, 8388608)
  assert_eq(limits.asset_response_timeout_millis, 10000)
  let wire = encode_asset_response("asset-4", {
    status: 200,
    mime_type: "text/plain",
    encoding: "utf-8",
    headers: [{ name: "Cache-Control", value: "no-store" }],
    body: b"ok",
  })
  let fields = decode_fields(wire)
  assert_eq(decode_field(fields, "request_id"), "asset-4")
  assert_eq(decode_field(fields, "status"), "200")
  assert_eq(decode_field(fields, "mime"), "text/plain")
  assert_eq(decode_field(fields, "encoding"), "utf-8")
  assert_eq(decode_field(fields, "body"), "b2s")
  assert_true(decode_field(fields, "headers") != "")
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
  let fields = decode_fields(wire)
  assert_eq(decode_field(fields, "origin"), "https://app.orbit.invalid")
  assert_eq(decode_field(fields, "initial_kind"), "html")
  assert_eq(decode_field(fields, "initial"), "<main>orbit</main>")
  assert_eq(
    decode_field(fields, "base_url"),
    "https://app.orbit.invalid/index.html",
  )
  assert_eq(decode_field(fields, "scripts"), "d2luZG93Lm9yYml0UmVhZHkgPSB0cnVl")
  assert_eq(decode_field(fields, "max_page_message_bytes"), "4096")
  assert_eq(decode_field(fields, "max_pending_asset_requests"), "4")
  assert_eq(decode_field(fields, "max_asset_response_bytes"), "8192")
  assert_eq(decode_field(fields, "asset_response_timeout_millis"), "250")
}
