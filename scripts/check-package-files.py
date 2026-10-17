#!/usr/bin/env python3
"""Check the pinned MoonBit package listing, without publishing or syncing dependencies."""
import argparse
from pathlib import Path, PurePosixPath
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
REQUIRED = {
    '.moon-version', '.moonignore', 'moon.mod', 'README.md', 'LICENSE',
    'src/moon.pkg', 'src/android/moon.pkg', 'src/webview/moon.pkg',
    'src/android/ajni_bridge.c', 'src/android/ajni_android_internal.h',
    'src/android/android_compat.h', 'src/webview/ajni_webview_bridge.c',
    'src/android_runtime/moon.pkg', 'src/android_runtime/main.mbt',
    'android/app/src/main/cpp/CMakeLists.txt',
    'android/host/src/main/java/dev/nanaloveyuki/ajni/host/NativeBridge.kt',
    'android/host/src/main/java/dev/nanaloveyuki/ajni/host/AjniWebViewHost.kt',
    'android/host/consumer-rules.pro', 'scripts/check-package-files.py',
    'scripts/check-jni-contract.py', 'android/gradle/wrapper/gradle-wrapper.jar',
    'android/gradle/wrapper/gradle-wrapper.properties', 'scripts/build-android.ps1',
    'src/runtime/moon.pkg', 'src/runtime/ajni_runtime.c', 'src/runtime/ajni_runtime.h',
    'src/unsafe/moon.pkg', 'src/invocation/moon.pkg',
    'java/src/main/java/dev/nanaloveyuki/ajni/AjniCallback.java',
    'java/src/main/java/dev/nanaloveyuki/ajni/AjniException.java',
    'scripts/run-jvm-smoke.py', 'tests/jvm/moon.mod', 'tests/jvm/main.mbt',
    'tests/invocation/moon.mod', 'tests/invocation/main.mbt',
}
FORBIDDEN_PARTS = {
    '.git', '.hg', '.svn', '.gradle', '.kotlin', '.cxx', '.cmake', '.mooncakes',
    '.idea', '.vscode', '.settings', '.cache', 'CMakeFiles', '__pycache__', '.externalNativeBuild',
}


def forbidden(path):
    parts = path.parts
    return (parts[0] in {'tmp', 'ref', '_build', '.moon', '.mooncakes'}
            or bool(FORBIDDEN_PARTS.intersection(parts))
            or path.as_posix() == 'android/local.properties'
            or path.as_posix().startswith('tests/ohos/')
            or path.as_posix() in {'scripts/build-ohos-mvp.py', 'scripts/run-ohos-mvp.py'}
            or (parts[0] == 'android' and 'build' in parts)
            or path.suffix == '.iml'
            or path.name in {'CMakeCache.txt', 'CMakeFiles'})


def check_listing(stdout, stderr, root=ROOT):
    files, errors = set(), []
    for line in (stdout + '\n' + stderr).splitlines():
        # Diagnostics are not paths. Only consume whole, root-relative file lines.
        if not line or line != line.strip():
            continue
        normalized = line.replace('\\', '/')
        path = PurePosixPath(normalized)
        if not path.parts:
            continue
        if forbidden(path):
            errors.append(f'forbidden package input: {path}')
            continue
        if path.is_absolute() or '..' in path.parts or (len(normalized) > 1 and normalized[1] == ':'):
            errors.append(f'path escape/invalid relative path: {line}')
            continue
        if ':' in normalized:
            continue
        candidate = root / path.as_posix()
        if candidate.is_file():
            if not candidate.resolve().is_relative_to(root.resolve()):
                errors.append(f'path escapes repository: {path}')
            else:
                files.add(path.as_posix())
    missing = sorted(REQUIRED - files)
    if missing:
        errors.append('cannot parse complete listing / missing publication inputs: '
                      + ', '.join(missing))
    if errors:
        raise ValueError('\n'.join(errors))
    return files


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stdout-file', type=Path, help='Check a captured listing instead of invoking moon')
    parser.add_argument('--stderr-file', type=Path, help='Captured stderr (requires --stdout-file)')
    args = parser.parse_args()
    if args.stderr_file and not args.stdout_file:
        parser.error('--stderr-file requires --stdout-file')
    try:
        if args.stdout_file:
            stdout = args.stdout_file.read_text()
            stderr = args.stderr_file.read_text() if args.stderr_file else ''
        else:
            result = subprocess.run(['moon', 'package', '--list', '--frozen'], cwd=ROOT,
                                    text=True, capture_output=True)
            stdout, stderr = result.stdout, result.stderr
            if result.returncode:
                print(stdout, end='', file=sys.stderr)
                print(stderr, end='', file=sys.stderr)
                print(f'moon package failed ({result.returncode}); listing not accepted', file=sys.stderr)
                return 1
        files = check_listing(stdout, stderr)
    except (OSError, ValueError) as error:
        print(error, file=sys.stderr)
        return 1
    print(f'Package file gate passed: {len(files)} real files; {len(REQUIRED)} required inputs present')
    return 0


if __name__ == '__main__':
    sys.exit(main())
