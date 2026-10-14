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

Install toolchain archive `0.10.14+7d59c7ec9` and Java 17. Its `moon` CLI version
is `0.1.20260920`, recorded in `.moon-version`; archive IDs are compiler versions,
not CLI versions. For Linux/macOS, pass `0.10.14+7d59c7ec9` to the official
installer. Android build work additionally needs Android SDK platform 35,
platform tools, NDK `29.0.14206865`, CMake `4.1.2`, and the bundled Gradle
`8.10.2` wrapper.

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
./android/gradlew -p android :host:checkJniContract :host:lintDebug :host:assembleDebug \
  :app:assembleDebug :app:assembleDebugAndroidTest :app:lintDebug --no-daemon --stacktrace
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

## CI Infrastructure

`.github/actions/setup-moonbit` installs the compiler-versioned archive on Linux
and verifies both the `.moon-version` CLI pin and exact compiler version.
`Build` separates MoonBit/package checks from Android JNI/lint/build checks;
the always-running `ci` job fails if either job fails, is cancelled, or is skipped.
Configure branch protection to require `ci` (replacing the former `checks` job)
and at least one review. Workflows do not alter repository protection settings.
Pull requests use read-only permissions and Gradle caches; only a release job
receives `contents: write`. Weekly Dependabot updates cover GitHub Actions only,
not the deliberately pinned compiler, Gradle, AGP or Android toolchain.

Artifacts from successful jobs:

| Name | Contents | Retention |
| --- | --- | --- |
| `ajni-package` | `moon package --frozen` source ZIP | 30 days |
| `ajni-android-debug` | Demo APK, instrumentation APK, host AAR | 30 days |
| `ajni-api-docs` | `moon doc --frozen` API data, not a static site | 14 days |
| `ajni-android-reports` | Available Android reports, also uploaded on failure | 14 days |

## Release Process

1. Update `moon.mod` and add a nonempty `## X.Y.Z` entry to `CHANGELOG.md` in a
   reviewed PR. Use stable numeric versions; prerelease tags are not supported.
2. After merging to `main`, validate and push the matching new tag:

   ```sh
   python3 scripts/check-release.py --tag vX.Y.Z
   git tag -a vX.Y.Z -m "ajni X.Y.Z"
   git push origin vX.Y.Z
   ```

3. `Release` validates metadata and `main` ancestry, calls the same Build workflow,
   downloads its artifacts, and creates a GitHub Release containing the MoonBit
   source ZIP, `ajni-android-debug-vX.Y.Z.zip`, and `SHA256SUMS`. No additional
   GitHub secret is needed. Do not reuse an existing release tag; failed checks
   can be rerun, but existing releases are not updated or overwritten.

The Android ZIP preserves `app/build/outputs/...` and `host/build/outputs/...`.
The host AAR ships Kotlin classes and consumer keep rules; native `libajni.so`
is integrated separately through the bundled CMake template, not embedded in
that AAR. Debug APKs use the runner's debug key and are not production releases.
CI success proves the documented static/build gates only, never device runtime.

### Optional Mooncakes Publishing

GitHub Releases work without a registry credential. To additionally publish the
same validated source ZIP to Mooncakes after GitHub Release creation:

1. Log in to the intended Mooncakes owner account on a trusted workstation using
   `moon login`. The official [package publishing guide](https://docs.moonbitlang.com/en/latest/toolchain/moon/package-manage-tour.html#setup-mooncakes-io-account)
   records the credential location as `~/.moon/credentials.json`.
2. Store that complete JSON file as the repository Actions secret
   `MOONCAKES_CREDENTIALS`, not a GitHub PAT or a bare token. Set the repository
   Actions variable `MOONCAKES_PUBLISH` to `true` to enable the job. The JSON shape
   is `{"token":"...","username":"..."}`; `username` is optional.
3. The release job writes credentials with mode `0600` only for publishing,
   extracts the validated source artifact, runs `moon publish --frozen`, and
   removes credentials on exit and in an always-running cleanup step. It does
   not expose the secret to pull requests or cache/upload MoonBit home.

Configure without printing the credential (replace the path when `MOON_HOME`
differs from the default):

```sh
gh secret set MOONCAKES_CREDENTIALS --repo Nanaloveyuki/ajni < "$HOME/.moon/credentials.json"
gh variable set MOONCAKES_PUBLISH --repo Nanaloveyuki/ajni --body true
```

This follows the official [MoonBit async publishing workflow](https://github.com/moonbitlang/async/blob/main/.github/workflows/publish.yml).
When disabled the registry job is skipped; when enabled, missing/malformed
credentials fail it rather than silently skipping publication. A failed registry
job leaves the GitHub Release intact; rerun failed jobs after correcting the
credential. If the registry already accepted the version, do not republish it.
Local smoke validates schema/path/permissions/cleanup with synthetic credentials
only; actual registry acceptance requires the owner's real secret and a new tag.
