///|
/// Lossy UTF-8 decoding with an initial BOM ignored. Malformed sequences become
/// U+FFFD; WebView event decoding instead preserves BOM and drops invalid bytes.
pub fn decode_utf8(bytes : BytesView) -> String {
  @utf8.decode_lossy(bytes[:], ignore_bom=true)
}

///|
/// Encodes well-formed UTF-16 without BOM; invalid surrogate pairs panic.
pub fn encode_utf8(text : String) -> Bytes {
  @utf8.encode(text[:], bom=false)
}
