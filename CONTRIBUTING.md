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

## Development Setup

Install MoonBit and Java 17. Android build work additionally needs Android SDK
platform tools, NDK `29.0.14206865`, CMake `4.1.2`, and Gradle `8.10.2`.

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

Run the narrowest relevant checks first, then the shared validation chain:

```powershell
moon fmt --check
moon check --target native
moon test --target native -v
```

For Android JNI, Kotlin host, CMake, or ABI changes, build the demo:

```powershell
.\scripts\build-android.ps1
```

Run Android instrumentation tests on an emulator or device when a change
affects `WebView`, UI-thread delivery, Activity/container teardown, JNI UTF-16
conversion, or Surface behavior. GitHub Actions builds the Android demo for
`arm64-v8a` and `x86_64` on every pull request.

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
