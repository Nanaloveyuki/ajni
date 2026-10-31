# Harmony Compatibility: Isolated NAPI MVP

Status: emulator-validated MVP only, 2026-10-17. This ADR records the isolated HarmonyOS/OpenHarmony validation boundary; it does not add a published `ajni/ohos` package or change the JNI API. No physical HarmonyOS/OpenHarmony device is available; hardware validation is deferred and is not a prerequisite for retaining this private MVP.

## Decision

Keep `ajni` JNI-specific. Validate a separate Node-API adapter in `tests/ohos/` before considering any public platform package. The fixture uses MoonBit-generated C, the matching pinned runtime sources, an ArkTS Stage host, and a native NAPI bridge. The owner thread is the ArkTS environment thread; native workers only enqueue owned payloads through a bounded TSFN. MoonBit managed entry, JS values, and environment handles remain owner-thread-only.

The test adapter is intentionally private. It covers synchronous managed calls, synchronous callback reentry, exact UTF-16 edge values, ArrayBuffer bytes, recoverable MoonBit errors, original JavaScript exception identity, wrong-thread rejection, worker callback delivery, bounded queue ownership, stale-generation close, active producer stop, idempotent close, and reopen. `holdWorker` exists only to make producer teardown deterministic.

## Reproducible Commands

The pinned toolchain and generated C are staged by:

```sh
python3 scripts/build-ohos-mvp.py --host-smoke --stage-only
```

This runs the Linux managed embedding smoke first. It passed with synchronous reentry, UTF-16 units, binary bytes, recoverable error handling, and worker state transitions.

With DevEco Studio installed, the target build and HAP staging are:

```sh
python3 scripts/build-ohos-mvp.py
```

The runner invokes the Windows SDK tools through PowerShell, keeps staging outside the repository, preserves a local signing profile when present, and rejects an unowned nonempty staging directory. The current build produced x86_64 and arm64-v8a ELF libraries and an unsigned debug HAP.

The current runner requires Windows DevEco tools, Windows PowerShell, and either WSL path conversion or native Windows paths; `--host-smoke` additionally requires Linux and a C compiler. Override installation/staging paths with `--deveco` and `--stage`. This is not yet a portable standalone OpenHarmony SDK runner.

The device runner installs the HAP, records the target identity, uses a unique run ID, and requires a matching PASS log line:

```sh
python3 scripts/run-ohos-mvp.py --hap <unsigned-or-signed-hap> --timeout 40
```

## Observed Evidence

The connected emulator reported `x86_64`, API `26`, and `OpenHarmony-7.0.0.105`. Despite that product string, this is the installed DevEco/HarmonyOS SDK emulator; it is evidence for this exact emulator image, not for an independent OpenHarmony device or all API 26 systems.

The real emulator run passed both `cleanup` and `suite`. The suite reported 13 scenarios, including:

- synchronous callback reentry and stateful managed calls;
- UTF-16 NUL/CJK/emoji/isolated-surrogate cases and `00 80 FF` bytes;
- recoverable negative-delta and callback-throw paths;
- owner-thread rejection and eight ordered TSFN worker callbacks;
- bounded queue rejection and exact payload release;
- stale callback discard, active producer join, idempotent close, and reopen.

The cleanup process reported `liveSessions: 0`, `livePayloads: 0`, `cleanupHooks: 1`, and `ownerTerminated: true` after terminating the Worker that owned the runtime. Its ready snapshot showed `accepted: 32`, `queueFull: 33`, and `workerRunning: true`. The adapter enforces its own exact 32-payload limit: the 64-item burst rejects 32 payloads and the held producer rejects one more, regardless of the system TSFN's internal queue bookkeeping.

`llvm-readelf` identified x86_64 and AArch64 shared objects linked against the target NAPI, logging, C++ runtime and libc libraries, without host absolute library paths. The build initially exposed an SDK26 compiler warning promoted to an error by `-Werror`; clearing the SDK's obsolete external GCC-toolchain hint for this self-contained Clang fixed it without weakening warnings in the probe.

Latest device identity and full results are written to `_build/ohos-device/device.json`, `suite.log`, and `cleanup.log`. The final build emitted only the expected missing-signing-profile warning; the earlier package-version and unchecked Worker-termination warnings were corrected. Publication file checks passed with the private fixture and its runners excluded.

## Not Yet Proven

- No independent OpenHarmony SDK/device build or runtime was performed.
- No physical arm64 device runtime was performed; arm64 was compile/link checked only.
- ArkWeb, WebView security semantics, XComponent, resource interception, and public NAPI API design remain out of scope.
- The HAP is unsigned because no project signing profile was supplied. The emulator accepted the debug HAP installation; release signing remains a host-owned setup step.
- After the owning environment terminates, the fixture rejects further managed use in that process; reopening after an ordinary session close is tested, but migration to a replacement environment is not supported.

The fixture can guide a later adapter design, but its passing result does not justify claiming binary or API compatibility across HarmonyOS NEXT and OpenHarmony, nor publishing the private test surface.

## Rollout Gates

1. Keep this fixture isolated while the MVP is reviewed; do not turn its private diagnostic functions into a public API.
2. When equipment becomes available, validate the same boundary with an independent OpenHarmony SDK/device and a physical arm64 HarmonyOS target, recording each SDK/API/ABI separately before claiming dual-family support. Until then, retain emulator-only acceptance; do not infer hardware compatibility.
3. Only then design a platform adapter around owner-thread dispatch and lifecycle. Keep JNI declarations/runtime unchanged rather than forcing NAPI into JNI types.
4. Validate ArkWeb separately with a host-declared Web component: controller attachment, script evaluation, trusted-origin messaging, resource loading and teardown must pass before a shared WebView backend is considered.

