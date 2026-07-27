///|
/// Events emitted by an Android WebView owned by the Kotlin host. Payloads are
/// UTF-8 normalised before entering MoonBit.
pub(all) enum EventKind {
  Created(String)
  NavigationStarted(String)
  NavigationFinished(String)
  TitleChanged(String)
  /// JavaScript evaluation completion as `(request_id, JSON result)`.
  ScriptResult(String, String)
  Failed(String)
  /// A message accepted by the host's trusted-origin WebMessage listener.
  PageMessage(String, String, Bool)
  /// A request intercepted by the host's embedded asset loader.
  AssetRequest(AssetRequest)
  /// An asynchronous command failure correlated with the supplied operation ID.
  OperationFailed(String, String)
  Destroyed
} derive(Eq, Debug)

///|
/// The mutually exclusive first document loaded by a newly created WebView.
pub(all) enum InitialContent {
  Empty
  Url(String)
  Html(String, String)
} derive(Eq, Debug)

///|
/// Per-view host quotas. Zero disables an individual quota; negative values
/// are rejected before the command crosses JNI.
pub struct WebViewResourceLimits {
  max_page_message_bytes : Int
  max_pending_asset_requests : Int
  max_asset_response_bytes : Int
  asset_response_timeout_millis : Int
} derive(Eq, Debug)

///|
pub fn WebViewResourceLimits::new(
  max_page_message_bytes? : Int = 1048576,
  max_pending_asset_requests? : Int = 32,
  max_asset_response_bytes? : Int = 8388608,
  asset_response_timeout_millis? : Int = 10000,
) -> WebViewResourceLimits {
  {
    max_page_message_bytes,
    max_pending_asset_requests,
    max_asset_response_bytes,
    asset_response_timeout_millis,
  }
}

///|
/// An embedded asset request. The path is normalised by the Kotlin host before
/// it enters MoonBit and must be answered through `respond_asset`.
pub struct AssetRequest {
  id : String
  path : String
} derive(Eq, Debug)

///|
/// A response header for an embedded asset. Header names and values are passed
/// as UTF-8 bytes and must not contain transport delimiters.
pub struct AssetHeader {
  name : String
  value : String
} derive(Eq, Debug)

///|
pub fn AssetHeader::new(name~ : String, value~ : String) -> AssetHeader {
  { name, value }
}

///|
/// A complete embedded-asset response. The host owns timeout and pending
/// request limits; this package only serialises the accepted response.
pub struct AssetResponse {
  status : Int
  mime_type : String
  encoding : String
  headers : Array[AssetHeader]
  body : Bytes
} derive(Eq, Debug)

///|
pub fn AssetResponse::new(
  status~ : Int,
  mime_type~ : String,
  encoding? : String = "utf-8",
  headers? : Array[AssetHeader] = [],
  body? : Bytes = b"",
) -> AssetResponse {
  { status, mime_type, encoding, headers, body }
}

///|
/// A WebView callback paired with the caller-provided handle.
pub struct Event {
  handle : Int64
  kind : EventKind
} derive(Eq, Debug)
