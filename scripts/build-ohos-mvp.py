#!/usr/bin/env python3
"""Generate pinned MoonBit C and build the isolated Harmony MVP with DevEco."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / 'tests/ohos'


def run(command, *, cwd=None, env=None, capture=False):
    print('+', ' '.join(map(str, command)), flush=True)
    return subprocess.run(list(map(str, command)), cwd=cwd, env=env, check=True,
                          text=True, capture_output=capture)


def windows_path(path):
    if os.name == 'nt':
        return str(Path(path).resolve())
    return subprocess.check_output(['wslpath', '-w', str(Path(path).resolve())], text=True).strip()


def copy_tree(source, destination):
    shutil.copytree(source, destination, dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns('build', '.hvigor', 'oh_modules',
                                                 '.cxx', '.idea', 'local.properties'))


def host_smoke(generated, moon_home, build):
    if sys.platform != 'linux':
        raise RuntimeError('--host-smoke currently exercises Linux C embedding only')
    include = f'-I{moon_home / "include"}'
    obj = build / 'managed-host-smoke.o'
    executable = build / 'managed-host-smoke'
    run(['cc', '-std=gnu11', '-O2', '-fwrapv', '-fno-strict-aliasing', include,
         '-Dmain=ajni_ohos_unused_main', '-c', generated, '-o', obj])
    runtime = [moon_home / f'lib/runtime/{name}.c' for name in
               ('runtime', 'utf', 'env', 'sync_io', 'backtrace')]
    run(['cc', '-std=gnu11', '-O2', include, FIXTURE / 'native/host_smoke.c', obj,
         *runtime, '-lm', '-ldl', '-lpthread', '-o', executable])
    run([executable])


def stage(args):
    pin = (ROOT / '.moon-version').read_text().strip()
    version = run(['moon', 'version'], capture=True).stdout.strip()
    if not version.startswith(f'moon {pin} ('):
        raise RuntimeError(f'MoonBit {pin} required; found {version}')
    moon_home = Path(os.environ.get('MOON_HOME', Path.home() / '.moon')).resolve()
    build = ROOT / '_build/ohos-codegen'
    env = os.environ.copy()
    env['MOONBIT_NEW_NATIVE'] = '0'
    run(['moon', 'build', '--release', '--target', 'native', '--target-dir', build],
        cwd=FIXTURE, env=env)
    generated = list((build / 'native/release/build').rglob('*.c'))
    if len(generated) != 1:
        raise RuntimeError(f'Expected one generated C file, got {generated}')
    if args.host_smoke:
        host_smoke(generated[0], moon_home, build)
    destination = args.stage.resolve()
    if destination == ROOT or ROOT in destination.parents:
        raise RuntimeError('Windows build staging must be outside the repository')
    marker = destination / '.ajni-mvp-stage'
    if destination.exists() and any(destination.iterdir()) and not marker.is_file():
        raise RuntimeError('Refusing to overwrite a nonempty staging directory not owned by this runner')
    destination.mkdir(parents=True, exist_ok=True)
    signing_profile = destination / 'build-profile.local.json5'
    preserved_signing = signing_profile.read_bytes() if signing_profile.is_file() else None
    copy_tree(FIXTURE / 'app', destination)
    marker.write_text('ajni isolated Harmony validation project\n')
    if preserved_signing is not None:
        (destination / 'build-profile.json5').write_bytes(preserved_signing)
    if args.runtime_os == 'OpenHarmony':
        profile = destination / 'build-profile.json5'
        settings = json.loads(profile.read_text())
        for product in settings['app']['products']:
            product['runtimeOS'] = 'OpenHarmony'
            for key in ('compileSdkVersion', 'compatibleSdkVersion', 'targetSdkVersion'):
                product[key] = args.api
        profile.write_text(json.dumps(settings, indent=2) + '\n')
    native = destination / 'entry/src/main/cpp'
    native.mkdir(parents=True, exist_ok=True)
    copy_tree(FIXTURE / 'native', native)
    (native / 'generated').mkdir(exist_ok=True)
    shutil.copy2(generated[0], native / 'generated/probe.c')
    copy_tree(moon_home / 'include', native / 'moonbit/include')
    runtime = native / 'moonbit/runtime'
    runtime.mkdir(parents=True, exist_ok=True)
    for name in ('runtime', 'utf', 'env', 'sync_io', 'backtrace'):
        shutil.copy2(moon_home / f'lib/runtime/{name}.c', runtime / f'{name}.c')
    sdk = args.sdk_root.resolve() if args.sdk_root else args.deveco / 'sdk'
    (destination / 'local.properties').write_text(
        f'sdk.dir={windows_path(sdk).replace(chr(92), "/")}\n', encoding='utf-8')
    (destination / 'sdk-root.txt').write_text(windows_path(sdk) + '\n')
    # SDK26 ships Windows-only compilers. All native inputs are copied, never host objects.
    (destination / 'mvp-build.json').write_text(json.dumps({
        'moon': version, 'sdk': windows_path(sdk), 'abis': args.abi,
        'fixture': str(FIXTURE),
    }, indent=2) + '\n')
    profile = destination / 'entry/build-profile.json5'
    # The committed profile is strict JSON, also accepted as JSON5 by Hvigor.
    settings = json.loads(profile.read_text())
    native_options = settings.setdefault('buildOption', {}).setdefault('externalNativeOptions', {})
    native_options['abiFilters'] = args.abi
    profile.write_text(json.dumps(settings, indent=2) + '\n')
    return destination


def build(args, destination):
    node = args.deveco / 'tools/node/node.exe'
    hvigor = args.deveco / 'tools/hvigor/bin/hvigorw.js'
    ohpm = args.deveco / 'tools/ohpm/bin/pm-cli.js'
    tools = args.deveco / 'tools'
    env = os.environ.copy()
    # Windows node/npm must receive Windows PATH entries; avoid inheriting WSL paths.
    ps = shutil.which('pwsh.exe') or shutil.which('powershell.exe')
    if not ps:
        raise RuntimeError('Windows PowerShell is required to invoke the installed Windows SDK')
    script = destination / 'build-mvp.ps1'
    paths = {name: windows_path(path) for name, path in {
        'deveco': args.deveco, 'stage': destination, 'node': node, 'hvigor': hvigor,
        'ohpm': ohpm, 'tools': tools,
    }.items()}
    def quoted(value):
        return "'" + value.replace("'", "''") + "'"
    contents = ["$ErrorActionPreference = 'Stop'",
                "$env:DEVECO_SDK_HOME = " + quoted((destination / 'sdk-root.txt').read_text().strip()),
                "$env:JAVA_HOME = " + quoted(paths['deveco'] + '\\jbr'),
                "$env:NODE_HOME = " + quoted(paths['tools'] + '\\node'),
                "$env:OHPM_HOME = " + quoted(paths['tools'] + '\\ohpm'),
                "$env:HVIGOR_HOME = " + quoted(paths['tools'] + '\\hvigor'),
                "$env:Path = $env:NODE_HOME + ';' + $env:JAVA_HOME + '\\bin;' + $env:Path",
                'Set-Location ' + quoted(paths['stage']),
                '& ' + quoted(paths['node']) + ' ' + quoted(paths['ohpm']) + ' install --all --no-link',
                'if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }',
                '& ' + quoted(paths['node']) + ' ' + quoted(paths['hvigor']) +
                ' --no-daemon --mode module -p product=default -p buildMode=debug assembleHap',
                'exit $LASTEXITCODE']
    script.write_text('\n'.join(contents) + '\n')
    run([ps, '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', windows_path(script)], env=env)
    artifacts = sorted(destination.glob('entry/build/**/outputs/**/*.hap'))
    if not artifacts:
        raise RuntimeError('Hvigor finished without a HAP artifact')
    print('HAP artifacts:')
    for artifact in artifacts:
        print(artifact)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--deveco', type=Path,
                        default=Path(os.environ.get('DEVECO_HOME', '/mnt/e/Apps/DevEco Studio')))
    parser.add_argument('--stage', type=Path,
                        default=Path(os.environ.get('AJNI_OHOS_STAGE', '/mnt/e/tmp/ajni-ohos-mvp')))
    parser.add_argument('--abi', nargs='+', choices=('x86_64', 'arm64-v8a'),
                        default=['x86_64', 'arm64-v8a'])
    parser.add_argument('--sdk-root', type=Path,
                        help='SDK root for the selected family; defaults to DevEco/sdk')
    parser.add_argument('--runtime-os', choices=('HarmonyOS', 'OpenHarmony'), default='HarmonyOS')
    parser.add_argument('--api', type=int, default=26)
    parser.add_argument('--stage-only', action='store_true')
    parser.add_argument('--host-smoke', action='store_true',
                        help='Also exercise managed state, callback ownership and recovery on Linux')
    args = parser.parse_args()
    try:
        if not args.deveco.is_dir():
            raise RuntimeError('DevEco tools missing; pass --deveco or DEVECO_HOME')
        destination = stage(args)
        print(f'DevEco project: {windows_path(destination)}')
        if not args.stage_only:
            build(args, destination)
        return 0
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        print(error, file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
