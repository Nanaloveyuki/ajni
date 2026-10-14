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
python3 scripts/check-package-files.py
```

For Android JNI, Kotlin host, CMake, or ABI fixes, compile the demo only when
the required SDK/NDK/build tools are available; do not treat compilation as a
platform runtime test:

```powershell
.\scripts\build-android.ps1
```

For the full static Android acceptance, set `ANDROID_HOME`, `ANDROID_NDK_HOME`
and `MOON_HOME`, then run:

```sh
./android/gradlew -p android :host:checkJniContract :host:lintDebug \
  :app:assembleDebug :app:compileDebugAndroidTestKotlin :app:lintDebug --no-daemon --stacktrace
```

`checkJniContract` depends on typed `compileDebugKotlin` and its output provider,
uses actual bytecode without class initialization, and compiles temporary TUs
including real stubs with NDK API24 drivers (`arm64-v8a`/`x86_64`, production and
exports-disabled callback branches). The standalone CLI requires `--class-dir`,
`--ndk`, `--moon-home`; exit 2 means missing prerequisites, exit 1 a contract,
parsing or compilation failure. Do not substitute host headers or fake refcounts.
JNI varargs expression types still need manual review; keep rules do not prove
R8-minified lookup. Checker smoke may mutate temporary copies/captured javap,
never production source. Package checks use `moon package --list --frozen` after
dependency installation, consume both output streams, and fail on missing inputs.

Android/WebView device/emulator/runtime scenarios remain deferred. Real Java JNI
checks may run separately when authorized, with JVM version, actual C paths and
fixture limitations recorded; they do not validate production Kotlin or ART.
Compile MoonBit and instrumentation tests without executing them. Non-Android
operations report unsupported failures, not simulated ART/WebView success.
GitHub Actions compiles both ABIs/test sources and runs static lint without devices.

Keep observer mutation/main-thread contracts and snapshot reentrancy intact;
subscriptions survive ordinary shutdown. Instrumentation cleanup must clear the
observer, shut down, and reverse-detach tracked containers even after failures.
The Kotlin observer fires before JNI and is not end-to-end MoonBit evidence.
Current exercised checks and deferred scenarios are recorded in `BUGFIX_PLAN.md`.

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
