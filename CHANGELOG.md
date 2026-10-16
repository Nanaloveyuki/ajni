# Changelog

## 0.3.0

- Major feature release: ajni now supplies a general bidirectional JNI runtime, not only declarations and an Android event bridge. Hosts must initialize the matching MoonBit runtime and establish the owner-thread session explicitly; arbitrary foreign-thread MoonBit entry is unsupported. The pinned toolchain requires `0.x.y` package versions, so this release is `0.3.0`, not `1.0.0`; no additional platform/ABI support is implied.
- Split reusable CI into MoonBit and Android jobs with an aggregate gate, shared pinned toolchain setup, Gradle wrapper validation/read-only PR caches, and retained package/API/APK/AAR/lint artifacts. Keep MoonBit unit tests compile-only; execute the checked-JVM integration fixture.
- Add stable-tag release metadata/main-ancestry gates and validated GitHub Releases with source/Android integration ZIPs and SHA-256 checksums; scope weekly dependency updates to GitHub Actions.
- Add opt-in Mooncakes delivery of the validated source artifact using isolated step credentials, owner-supplied credential JSON, restrictive file permissions and unconditional cleanup.
- Fix CI installation to select archive `0.10.14+7d59c7ec9`, not the `moon` CLI ID `0.1.20260920` that returns HTTP 403; verify the exact compiler alongside the CLI pin without upgrading either.
- Request only `platform-tools` from Android SDK setup instead of its obsolete default `tools` package; keep the project's SDK/NDK/CMake versions unchanged.
- Implement checked bidirectional JNI in `runtime`: owner-thread sessions, global-reference handles, methods/constructors/fields, exact UTF-16 strings, primitive/object arrays, preserved Throwables and fixed Java callback dispatch with deferred cross-thread close.
- Add explicit raw `unsafe` host/native-registration APIs and optional `invocation` backed by `dynlib` 0.2.1; keep JVM creation once-only and its loader alive without `DestroyJavaVM` or prelinked `libjvm`.
- Migrate Android VM/env leases, exception and text helpers to the shared core; initialize MoonBit on explicit main-thread host initialization instead of `JNI_OnLoad`, and preserve generic sessions across feature shutdown.
- Add repeatable real JDK21 checked-JNI fixtures for Java/native hosting and CI execution. Validate nested-error isolation, callback lifetime, stale generations, Unicode, fields/arrays, Throwable identity and loader/create failures; add Android callback instrumentation and companion keep rules.
- Pin declaring/receiver/argument JNI references across synchronous reentry, resolve member reference types through the declaring class's loader, and reject non-Throwable `JavaThrown` payloads before JNI `Throw`; retain checked-JVM regressions.
- Read the release version directly from MoonBit's manifest syntax instead of parsing dependency import blocks as TOML; keep stable-version, exact-tag and nonempty-changelog gates.
- Verified locally: both checked-JVM host modes, native check/build-only tests, both-ABI Android JNI contract, debug APK/instrumentation compilation and lint; API37 x86_64 emulator passed all 8 instrumentation tests including actual Activity recreation, callback state/lifetime, handled observer errors and WebView paths. arm64/API24 runtime and R8 lookup remain unverified.

## 0.2.6

- Exclude research `tmp/` and observed Python bytecode cache from MoonBit publication; gate the frozen file list in CI without excluding build scripts.
- Add an exact JNI bytecode/C/prototype/consumer-rules gate and Gradle task using real API24 NDK compilers for both supported ABIs and export/callback configurations. Fix the Android `AttachCurrentThread` argument to its actual `JNIEnv **` prototype.
- Release the previous owned callback reference on every replacement, including identical addresses; remove unused classloader retention while preserving initialization ABI.
- Document observer threads, snapshot reentrancy, shutdown, inline WebView dispatch and canonical wire/text boundaries. Add independent-literal consumer and nested-handler regressions, compiled only.
- Make all four existing instrumentation tests unconditionally clear observers, shut down and reverse-detach owned containers; relay observer failures to the test thread and assert the production page-message wire literal.
- Public signatures and support scope remain unchanged. Static gates, build-only regressions, both-ABI debug APK compilation and lint passed. A real `-Xcheck:jni` JVM fixture passed 36 registration/exception/text/upcall/attachment/worker assertions; Android/WebView/refcount/R8 runtime verification remains deferred.

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
