param(
  [ValidateSet("Debug", "Release")]
  [string]$Configuration = "Debug"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$androidRoot = Join-Path $repoRoot "android"
$sdkRoot = if ($env:ANDROID_SDK_ROOT) {
  $env:ANDROID_SDK_ROOT
} elseif ($env:ANDROID_HOME) {
  $env:ANDROID_HOME
} else {
  Join-Path $env:LOCALAPPDATA "Android\Sdk"
}

if (-not (Test-Path (Join-Path $sdkRoot "platform-tools\adb.exe"))) {
  throw "Android SDK was not found. Set ANDROID_SDK_ROOT or install it with Android Studio."
}

$ndkRoot = $env:ANDROID_NDK_HOME
if (-not $ndkRoot) {
  $ndkCandidates = Get-ChildItem (Join-Path $sdkRoot "ndk") -Directory -ErrorAction SilentlyContinue |
    Sort-Object Name -Descending
  if ($ndkCandidates) { $ndkRoot = $ndkCandidates[0].FullName }
}
if (-not $ndkRoot -or -not (Test-Path (Join-Path $ndkRoot "build\cmake\android.toolchain.cmake"))) {
  throw "Android NDK is required. Install a side-by-side NDK from Android Studio SDK Manager, then set ANDROID_NDK_HOME if needed."
}

$env:ANDROID_SDK_ROOT = $sdkRoot
$env:ANDROID_NDK_HOME = $ndkRoot
$moon = Get-Command moon -ErrorAction SilentlyContinue
if (-not $moon) {
  throw "MoonBit was not found on PATH. Install MoonBit before building the Android runtime."
}
Push-Location $repoRoot
try {
  $moonVersion = (Get-Content (Join-Path $repoRoot ".moon-version") -Raw).Trim()
  $installedMoonVersion = & $moon.Source version
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
  if (-not (($installedMoonVersion -join "`n").StartsWith("moon $moonVersion ("))) {
    throw "MoonBit $moonVersion is required; found $installedMoonVersion."
  }
  & $moon.Source build --target native --release src/android_runtime
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} finally {
  Pop-Location
}
$gradle = Join-Path $androidRoot "gradlew.bat"
$systemGradle = Get-Command gradle -ErrorAction SilentlyContinue
if (-not (Test-Path $gradle) -and -not $systemGradle) {
  throw "Gradle Wrapper is absent and gradle is not on PATH. Import android/ in Android Studio or install Gradle 8.10.2."
}
Push-Location $androidRoot
try {
  $gradleCommand = if (Test-Path $gradle) { $gradle } else { $systemGradle.Source }
  $installedGradleVersion = & $gradleCommand --version
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
  if (-not ($installedGradleVersion -contains "Gradle 8.10.2")) {
    throw "Gradle 8.10.2 is required; found $($installedGradleVersion -join ' ')."
  }
  & $gradleCommand "assemble$Configuration"
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} finally {
  Pop-Location
}
