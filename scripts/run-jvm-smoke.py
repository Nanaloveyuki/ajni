#!/usr/bin/env python3
"""Build and exercise the real Java-hosted and native-hosted JNI paths on Linux."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def run(command, *, cwd, env):
    print('+', ' '.join(map(str, command)), flush=True)
    subprocess.run(list(map(str, command)), cwd=cwd, env=env, check=True)


def jdk_home():
    configured = os.environ.get('JAVA_HOME')
    if configured:
        home = Path(configured).resolve()
    else:
        javac = shutil.which('javac')
        if not javac:
            raise RuntimeError('JAVA_HOME or javac on PATH is required')
        home = Path(javac).resolve().parents[1]
    for relative in ('bin/java', 'bin/javac', 'include/jni.h', 'include/linux/jni_md.h'):
        if not (home / relative).is_file():
            raise RuntimeError(f'JDK input missing: {home / relative}')
    return home


def copy_module(destination):
    destination.mkdir()
    shutil.copy2(ROOT / 'moon.mod', destination / 'moon.mod')
    shutil.copytree(ROOT / 'src', destination / 'src')
    shutil.copytree(ROOT / 'tests', destination / 'tests',
                    ignore=shutil.ignore_patterns('.mooncakes', '_build', '__pycache__'))
    shutil.copytree(ROOT / 'java', destination / 'java')


def runtime_sources(moon_home):
    directory = moon_home / 'lib/runtime'
    if directory.is_dir():
        files = [directory / f'{name}.c' for name in
                 ('runtime', 'utf', 'env', 'sync_io', 'backtrace')]
    else:
        files = [moon_home / 'lib/runtime.c']
    for path in files:
        if not path.is_file():
            raise RuntimeError(f'MoonBit runtime input missing: {path}')
    return files


def java_host(work, module, build, home, moon_home, env):
    fixture = module / 'tests/jvm'
    run(['moon', 'build', '--release', '--target', 'native', '--target-dir', build,
         fixture], cwd=work, env=env)
    generated = list((build / 'native/release/build').rglob('*.c'))
    if len(generated) != 1:
        raise RuntimeError(f'Expected one generated JVM consumer C file, got {generated}')
    includes = [f'-I{moon_home / "include"}', f'-I{home / "include"}',
                f'-I{home / "include/linux"}', f'-I{module / "src/runtime"}']
    objects = []
    for index, source in enumerate((module / 'src/runtime/ajni_runtime.c', fixture / 'bridge.c')):
        obj = work / f'jni-{index}.o'
        run([env.get('CC', 'cc'), '-std=c11', '-O2', '-fPIC', '-Wall', '-Wextra',
             '-Werror', *includes, '-c', source, '-o', obj], cwd=work, env=env)
        objects.append(obj)
    library = work / 'libajni_smoke.so'
    run([env.get('CC', 'cc'), '-shared', '-fPIC', '-O2', *includes, *generated,
         *objects, *runtime_sources(moon_home), '-lm', '-ldl', '-lpthread',
         '-o', library], cwd=work, env=env)
    classes = work / 'classes'
    classes.mkdir()
    sources = sorted((module / 'java/src/main/java').rglob('*.java'))
    sources += sorted(fixture.glob('*.java'))
    sources += sorted((module / 'tests/invocation').glob('*.java'))
    run([home / 'bin/javac', '-d', classes, *sources], cwd=work, env=env)
    run([home / 'bin/java', '-Xcheck:jni', '-cp', classes,
         'dev.nanaloveyuki.ajni.fixture.Smoke', library], cwd=work, env=env)
    return classes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--java-only', action='store_true', help='Exercise Java hosting only')
    args = parser.parse_args()
    if sys.platform != 'linux':
        parser.error('This runner currently validates Linux JNI ABI only')
    try:
        home = jdk_home()
        moon_home = Path(os.environ.get('MOON_HOME', Path.home() / '.moon')).resolve()
        env = os.environ.copy()
        include_paths = [str(home / 'include'), str(home / 'include/linux')]
        if env.get('CPATH'):
            include_paths.append(env['CPATH'])
        env['CPATH'] = os.pathsep.join(include_paths)
        with tempfile.TemporaryDirectory(prefix='ajni-jvm-') as temporary:
            work = Path(temporary)
            module = work / 'ajni'
            copy_module(module)
            env['CPATH'] = os.pathsep.join([str(module / 'src/runtime'), env['CPATH']])
            members = ['ajni', 'ajni/tests/jvm']
            if not args.java_only:
                members.append('ajni/tests/invocation')
            run(['moon', 'work', 'init', *members], cwd=work, env=env)
            classes = java_host(work, module, work / 'java-build', home, moon_home, env)
            if not args.java_only:
                library = home / 'lib/server/libjvm.so'
                if not library.is_file():
                    raise RuntimeError(f'JVM shared library missing: {library}')
                fixture = module / 'tests/invocation'
                build = work / 'invocation-build'
                run(['moon', 'build', '--release', '--target', 'native', '--target-dir',
                     build, fixture], cwd=work, env=env)
                executables = list((build / 'native/release/build').rglob('*.exe'))
                if len(executables) != 1:
                    raise RuntimeError(f'Expected one invocation executable, got {executables}')
                unrelated_source = work / 'unrelated.c'
                unrelated_source.write_text('int unrelated_symbol(void) { return 42; }\n')
                unrelated_library = work / 'libunrelated.so'
                run([env.get('CC', 'cc'), '-shared', '-fPIC', unrelated_source,
                     '-o', unrelated_library], cwd=work, env=env)
                for mode, path in [('bad-path', work / 'missing.so'),
                                   ('missing-symbol', unrelated_library),
                                   ('create-failure', library), ('success', library)]:
                    run([executables[0], mode, path, classes], cwd=work, env=env)
        return 0
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(error, file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
