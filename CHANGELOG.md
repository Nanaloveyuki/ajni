# Changelog

## 0.2.5

- Fix JNI exception class references and unconditional exception handling; publish Android readiness only after initialization succeeds and propagate existing `JniError` categories.
- Enforce classfile MUTF-8 byte limits, reserving object-descriptor framing in class names; document static255/instance254 parameter slots and replace malformed ordinary UTF-8 with U+FFFD.
- Invalidate stale runtime/container/view work while preserving queued create-command ordering; inherit pause state, roll back failed creation, terminate asset requests correctly, and clean up renderer exit without retries.
- Ship precise JNI consumer rules, a checksum-pinned Gradle 8.10.2 wrapper, explicit MoonBit version verification, complete per-ABI incremental inputs, and PowerShell exit-code propagation.
- Non-Android operations now report unsupported `NativeFailure` instead of success. Public MoonBit signatures remain unchanged.
- Limit maintenance to bug/build fixes and documentation until MoonBit 1.0. Static checks and compile-only tests do not replace deferred Android/WebView runtime verification.

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
