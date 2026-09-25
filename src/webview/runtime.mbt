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
  if !@android.runtime_ready() ||
    native_set_bounds(handle, x, y, width, height) == 0 {
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
  if !@android.runtime_ready() ||
    native_command(
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
    30 =>
      match decode_utf8_payload(payload) {
        Some(value) => EventKind::Created(value)
        None => return
      }
    31 =>
      match decode_utf8_payload(payload) {
        Some(value) => NavigationStarted(value)
        None => return
      }
    32 =>
      match decode_utf8_payload(payload) {
        Some(value) => NavigationFinished(value)
        None => return
      }
    33 =>
      match decode_utf8_payload(payload) {
        Some(value) => TitleChanged(value)
        None => return
      }
    34 =>
      match (decode_utf8_payload(detail), decode_utf8_payload(payload)) {
        (Some(request_id), Some(result)) => ScriptResult(request_id, result)
        _ => return
      }
    35 =>
      match decode_utf8_payload(payload) {
        Some(value) => Failed(value)
        None => return
      }
    36 => Destroyed
    37 =>
      match decode_page_message(payload) {
        Some(event) => event
        None => return
      }
    38 =>
      match decode_asset_request(payload) {
        Some(event) => event
        None => return
      }
    39 =>
      match (decode_utf8_payload(detail), decode_utf8_payload(payload)) {
        (Some(request_id), Some(message)) =>
          OperationFailed(request_id, message)
        _ => return
      }
    _ => return
  }
  let event = { handle, kind: event_kind, }
  let snapshot : Array[(Event) -> Unit] = []
  for _, handler in handlers {
    snapshot.push(handler)
  }
  for handler in snapshot {
    handler(event)
  }
}

///|
fn decode_page_message(payload : Bytes) -> EventKind? {
  let wire = match decode_utf8_payload(payload) {
    Some(wire) => wire
    None => return None
  }
  let fields = match decode_fields(wire) {
    Some(fields) => fields
    None => return None
  }
  match (fields.get("body"), fields.get("origin"), fields.get("main_frame")) {
    (Some(body), Some(origin), Some(main_frame)) =>
      match main_frame {
        "1" | "true" => Some(PageMessage(body, origin, true))
        "0" | "false" => Some(PageMessage(body, origin, false))
        _ => None
      }
    _ => None
  }
}

///|
fn decode_asset_request(payload : Bytes) -> EventKind? {
  let wire = match decode_utf8_payload(payload) {
    Some(wire) => wire
    None => return None
  }
  let fields = match decode_fields(wire) {
    Some(fields) => fields
    None => return None
  }
  match (fields.get("request_id"), fields.get("path")) {
    (Some(id), Some(path)) => Some(AssetRequest({ id, path, }))
    _ => None
  }
}

///|
fn decode_fields(value : String) -> Map[String, String]? {
  let fields : Map[String, String] = Map([])
  if value.length() == 0 {
    return Some(fields)
  }

  for item in value.split("&") {
    let pair = item.split("=").to_array()
    if pair.length() != 2 {
      return None
    }
    let name = pair[0].to_owned()
    if !valid_field_name(name) || fields.contains(name) {
      return None
    }
    match decode_base64url(pair[1].to_owned()) {
      Some(decoded) => fields.set(name, decoded)
      None => return None
    }
  }
  Some(fields)
}

///|
fn valid_field_name(name : String) -> Bool {
  if name.length() == 0 {
    return false
  }
  for byte in @ajni.encode_utf8(name) {
    match byte {
      b'a'..=b'z' | b'_' => ()
      _ => return false
    }
  }
  true
}

///|
fn decode_utf8_payload(payload : Bytes) -> String? {
  let text = @utf8.decode(payload[:]) catch { _ => return None }
  Some(text)
}

///|
fn decode_base64url(value : String) -> String? {
  let output : Array[Byte] = []
  for byte in @ajni.encode_utf8(value) {
    output.push(
      match byte {
        b'-' => b'+'
        b'_' => b'/'
        b'=' => b'='
        b'A'..=b'Z' | b'a'..=b'z' | b'0'..=b'9' => byte
        _ => return None
      },
    )
  }
  let decoded = @base64.decode(@ajni.decode_utf8(Bytes::from_array(output))) catch {
    _ => return None
  }
  let text = @utf8.decode(decoded[:]) catch { _ => return None }
  Some(text)
}
