# Changelog

## 0.2.4

- Pin the project to MoonBit 0.1.20260920 (`moonc` 0.10.14).
- Keep derived `Eq` and `Debug` methods explicit so newer toolchains do not promote them implicitly.

## 0.2.3

- Pin the project to MoonBit 0.10.9.
- Keep Android event callback contexts alive during concurrent dispatch and JNI unload.
- Snapshot event handlers during dispatch and reject malformed WebView wire payloads.

## 0.2.2

- Validate the native JNI runtime and public interfaces with MoonBit 0.10.6.
- Preserve the existing MoonBit API and Android bridge behavior.
