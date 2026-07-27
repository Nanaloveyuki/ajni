///|
let handlers : Map[Int, (Event) -> Unit] = Map([])

///|
let next_handler_id : Ref[Int] = Ref(1)

///|
/// Registers a process-wide WebView event observer and returns its removal
/// token. Register before the Kotlin host emits callbacks.
pub fn install_event_handler(handler : (Event) -> Unit) -> Int {
  let id = next_handler_id.val
  next_handler_id.val = id + 1
  handlers.set(id, handler)
  id
}

///|
pub fn remove_event_handler(id : Int) -> Unit {
  ignore(handlers.remove(id))
}

///|
/// Creates a WebView in the Kotlin host's attached `FrameLayout` and starts
/// navigating to `url`. The command is always queued on Android's main Looper.
pub fn create(
  handle : Int64,
  trusted_origin : String,
  initial_content : InitialContent,
  document_start_scripts? : Array[String] = [],
  resource_limits? : WebViewResourceLimits = WebViewResourceLimits::new(),
  operation_id? : String = "",
) -> Unit raise @ajni.JniError {
  if !valid_resource_limits(resource_limits) {
    raise NativeFailure("WebView resource limits must not be negative")
  }
  let (initial_kind, initial, base_url) = match initial_content {
    Empty => ("empty", "", "")
    Url(url) => ("url", url, "")
    Html(html, base_url) => ("html", html, base_url)
  }
  command(
    6,
    handle,
    encode_create_options(
      trusted_origin, initial_kind, initial, base_url, document_start_scripts, resource_limits,
    ),
    operation_id,
  )
}

///|
fn encode_create_options(
  trusted_origin : String,
  initial_kind : String,
  initial : String,
  base_url : String,
  document_start_scripts : Array[String],
  resource_limits : WebViewResourceLimits,
) -> String {
  encode_fields([
    ("origin", trusted_origin),
    ("initial_kind", initial_kind),
    ("initial", initial),
    ("base_url", base_url),
    ("scripts", encode_string_list(document_start_scripts)),
    (
      "max_page_message_bytes",
      resource_limits.max_page_message_bytes.to_string(),
    ),
    (
      "max_pending_asset_requests",
      resource_limits.max_pending_asset_requests.to_string(),
    ),
    (
      "max_asset_response_bytes",
      resource_limits.max_asset_response_bytes.to_string(),
    ),
    (
      "asset_response_timeout_millis",
      resource_limits.asset_response_timeout_millis.to_string(),
    ),
  ])
}

///|
pub fn navigate(
  handle : Int64,
  url : String,
  operation_id? : String = "",
) -> Unit raise @ajni.JniError {
  command(2, handle, url, operation_id)
}

///|
pub fn load_html_with_base_url(
  handle : Int64,
  html : String,
  base_url : String,
  operation_id? : String = "",
) -> Unit raise @ajni.JniError {
  command(
    7,
    handle,
    encode_fields([("html", html), ("base_url", base_url)]),
    operation_id,
  )
}

///|
pub fn add_document_start_script(
  handle : Int64,
  script : String,
  operation_id? : String = "",
) -> Unit raise @ajni.JniError {
  command(10, handle, encode_fields([("script", script)]), operation_id)
}

///|
pub fn eval(
  handle : Int64,
  script : String,
  operation_id : String,
) -> Unit raise @ajni.JniError {
  command(4, handle, script, operation_id)
}

///|
pub fn post_message(
  handle : Int64,
  body : String,
  operation_id? : String = "",
) -> Unit raise @ajni.JniError {
  command(8, handle, encode_fields([("body", body)]), operation_id)
}

///|
pub fn respond_asset(
  handle : Int64,
  request_id : String,
  response : AssetResponse,
  operation_id? : String = "",
) -> Unit raise @ajni.JniError {
  command(9, handle, encode_asset_response(request_id, response), operation_id)
}

///|
pub fn destroy(
  handle : Int64,
  operation_id? : String = "",
) -> Unit raise @ajni.JniError {
  command(5, handle, "", operation_id)
}

///|
pub fn set_bounds(
  handle : Int64,
  x : Int,
  y : Int,
  width : Int,
  height : Int,
) -> Unit raise @ajni.JniError {
  if native_set_bounds(handle, x, y, width, height) == 0 {
    raise NotInitialized
  }
}

///|
fn command(
  command : Int,
  handle : Int64,
  payload : String,
  request_id : String,
) -> Unit raise @ajni.JniError {
  if native_command(
      command,
      handle,
      @ajni.encode_utf8(payload),
      @ajni.encode_utf8(request_id),
    ) ==
    0 {
    raise NotInitialized
  }
}

///|
fn encode_asset_response(
  request_id : String,
  response : AssetResponse,
) -> String {
  let headers : Array[String] = []
  for header in response.headers {
    headers.push(
      encode_fields([("name", header.name), ("value", header.value)]),
    )
  }
  encode_fields([
    ("request_id", request_id),
    ("status", response.status.to_string()),
    ("mime", response.mime_type),
    ("encoding", response.encoding),
    ("headers", encode_string_list(headers)),
    ("body", base64url(response.body)),
  ])
}

///|
fn encode_fields(fields : Array[(String, String)]) -> String {
  let encoded : Array[String] = []
  for field in fields {
    encoded.push("\{field.0}=\{base64url(@ajni.encode_utf8(field.1))}")
  }
  encoded.join("&")
}

///|
fn encode_string_list(values : Array[String]) -> String {
  let encoded : Array[String] = []
  for value in values {
    encoded.push(base64url(@ajni.encode_utf8(value)))
  }
  encoded.join(",")
}

///|
fn base64url(bytes : BytesView) -> String {
  let output : Array[Byte] = []
  for byte in @ajni.encode_utf8(@base64.encode(bytes, padding=false)) {
    output.push(
      match byte {
        b'+' => b'-'
        b'/' => b'_'
        _ => byte
      },
    )
  }
  @ajni.decode_utf8(Bytes::from_array(output))
}

///|
fn valid_resource_limits(limits : WebViewResourceLimits) -> Bool {
  limits.max_page_message_bytes >= 0 &&
  limits.max_pending_asset_requests >= 0 &&
  limits.max_asset_response_bytes >= 0 &&
  limits.asset_response_timeout_millis >= 0
}

///|
/// Stable C export for UTF-8 callbacks from the optional Android WebView stub.
pub fn dispatch_from_android(
  kind : Int,
  handle : Int64,
  payload : Bytes,
  detail : Bytes,
) -> Unit {
  let event_kind = match kind {
    30 => EventKind::Created(@ajni.decode_utf8(payload))
    31 => NavigationStarted(@ajni.decode_utf8(payload))
    32 => NavigationFinished(@ajni.decode_utf8(payload))
    33 => TitleChanged(@ajni.decode_utf8(payload))
    34 => ScriptResult(@ajni.decode_utf8(detail), @ajni.decode_utf8(payload))
    35 => Failed(@ajni.decode_utf8(payload))
    36 => Destroyed
    37 => decode_page_message(payload)
    38 => decode_asset_request(payload)
    39 => OperationFailed(@ajni.decode_utf8(detail), @ajni.decode_utf8(payload))
    _ => return
  }
  let event = { handle, kind: event_kind }
  for _, handler in handlers {
    handler(event)
  }
}

///|
fn decode_page_message(payload : Bytes) -> EventKind {
  let fields = decode_fields(@ajni.decode_utf8(payload))
  PageMessage(
    decode_field(fields, "body"),
    decode_field(fields, "origin"),
    match decode_field(fields, "main_frame") {
      "1" | "true" => true
      _ => false
    },
  )
}

///|
fn decode_asset_request(payload : Bytes) -> EventKind {
  let fields = decode_fields(@ajni.decode_utf8(payload))
  AssetRequest({
    id: decode_field(fields, "request_id"),
    path: decode_field(fields, "path"),
  })
}

///|
fn decode_fields(value : String) -> Map[String, String] {
  let fields : Map[String, String] = Map([])
  for item in value.split("&") {
    let pair = item.split("=").to_array()
    if pair.length() == 2 {
      fields.set(pair[0].to_owned(), decode_base64url(pair[1].to_owned()))
    }
  }
  fields
}

///|
fn decode_field(fields : Map[String, String], name : String) -> String {
  fields.get(name).unwrap_or("")
}

///|
fn decode_base64url(value : String) -> String {
  let output : Array[Byte] = []
  for byte in @ajni.encode_utf8(value) {
    output.push(
      match byte {
        b'-' => b'+'
        b'_' => b'/'
        _ => byte
      },
    )
  }
  @ajni.decode_utf8(
    @base64.decode_lossy(@ajni.decode_utf8(Bytes::from_array(output))),
  )
}
