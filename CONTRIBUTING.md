# Contributing To ajni

## Scope

`ajni` is an Android JNI runtime, not a window toolkit or application
framework. Keep JNI ownership, Kotlin host behavior, and MoonBit public API
explicit. The root `Nanaloveyuki/ajni` package must remain usable without
linking the optional `Nanaloveyuki/ajni/webview` native stub.

Discuss changes that alter JNI class names, Kotlin/native method signatures,
public MoonBit APIs, Android API levels, or the feature split before starting
implementation. Focused fixes, tests, and documentation improvements can be
proposed directly.

Until MoonBit 1.0 is officially released, work is limited to bug fixes in
existing behavior, build correctness, and documentation. Do not add general
JNI APIs, binding generators, new platform/ABI support, or additional WebView
features. Use [BUGFIX_PLAN.md](BUGFIX_PLAN.md) to track defect evidence and
static acceptance criteria; reconsider feature expansion after 1.0.

## Development Setup

Install the MoonBit version in `.moon-version` and Java 17. Android build work
additionally needs Android SDK platform 35, platform tools, NDK `29.0.14206865`,
CMake `4.1.2`, and the bundled Gradle `8.10.2` wrapper.

Set `ANDROID_NDK_HOME` when the NDK is not discoverable through Android Studio:

```powershell
$env:ANDROID_NDK_HOME = "C:\path\to\android-ndk-r29"
```

The repository does not vendor Android SDK, NDK, Gradle caches, MoonBit build
output, or generated APKs.

## Package Boundaries

- `src/` is the core JNI runtime. Do not add browser commands or browser event
  types here.
- `src/webview/` owns the optional WebView MoonBit API and
  `ajni_webview_bridge.c` native stub.
- `android/host/` contains the stable Kotlin JNI host class
  `dev.nanaloveyuki.ajni.host.NativeBridge`.
- `android/app/` is a demo consumer, not the public host contract.

WebView changes must update the MoonBit feature package, its JNI stub, Kotlin
host, CMake feature linkage, package docs, and focused tests together. Preserve
the core-only build path.

## Validate Changes

The current validation scope is static checking; Android/WebView runtime tests
are deferred. Run:

```powershell
moon fmt --check
moon check --target native
moon test --target native --build-only
moon info --target native
```

For Android JNI, Kotlin host, CMake, or ABI fixes, compile the demo only when
the required SDK/NDK/build tools are available; do not treat compilation as a
platform runtime test:

```powershell
.\scripts\build-android.ps1
```

Do not require device/emulator instrumentation, WebView UI smoke, or checked-JVM
runtime scenarios during this maintenance period. Compile existing tests without
executing them. Non-Android operations report unsupported failures, not simulated
ART/WebView success. Record static checks and compilation separately from deferred
runtime verification. GitHub Actions compiles Android and instrumented-test
sources, builds `arm64-v8a` and `x86_64`, and runs static lint without devices.

Update `README.md`, package `README.mbt.md` files, generated public interfaces,
and tests whenever observable behavior changes. Do not commit `_build`,
`.mooncakes`, Android SDK/NDK files, Gradle caches, or IDE output.

## Pull Requests

- Branch from current `main` and keep each PR focused.
- Describe the JNI/Kotlin/MoonBit surface affected and list validation run.
- Include the Android API and ABI scope for native changes.
- Keep the core and WebView feature boundary intact.
- All required GitHub Actions checks must pass.
- A pull request targeting `main` requires at least one developer LGTM before
  merge.
