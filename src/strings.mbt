///|
/// Decodes UTF-8 received from native code. Invalid sequences, including a
/// replacement emitted for isolated Java UTF-16 surrogates, are lossily
/// normalised instead of allowing invalid text into the public API.
pub fn decode_utf8(bytes : BytesView) -> String {
  @utf8.decode_lossy(bytes[:], ignore_bom=true)
}

///|
pub fn encode_utf8(text : String) -> Bytes {
  @utf8.encode(text[:], bom=false)
}
