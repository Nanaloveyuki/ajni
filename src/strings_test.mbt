///|
test "UTF-8 text round trips through MoonBit bytes" {
  inspect(
    decode_utf8(encode_utf8("JNI \u4e2d\u6587 \u{1F680}")[:]),
    content="JNI \u4e2d\u6587 \u{1F680}",
  )
}
