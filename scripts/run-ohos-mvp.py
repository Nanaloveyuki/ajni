#!/usr/bin/env python3
"""Install and execute the real Harmony MVP on one HDC-connected target."""
import argparse
import json
import os
from pathlib import Path
import queue
import subprocess
import sys
import threading
import time
import uuid

BUNDLE = 'dev.nanaloveyuki.ajni.ohosmvp'


def run(command, *, capture=True):
    print('+', ' '.join(map(str, command)), flush=True)
    result = subprocess.run(list(map(str, command)), text=True, capture_output=capture,
                            check=True, timeout=60)
    if capture and result.stdout:
        print(result.stdout, end='', flush=True)
    return result


def windows_path(path):
    if os.name == 'nt':
        return str(Path(path).resolve())
    return subprocess.check_output(['wslpath', '-w', str(Path(path).resolve())], text=True).strip()


def stream_lines(process, output):
    for line in process.stdout:
        output.put(line)
    output.put(None)


def scenario(hdc, mode, args):
    run([*hdc, 'shell', 'aa', 'force-stop', BUNDLE])
    run_id = uuid.uuid4().hex
    output = queue.Queue()
    logger = subprocess.Popen([*hdc, 'shell', 'hilog', '-T', 'AjniOhosMvp'],
                              text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              bufsize=1)
    threading.Thread(target=stream_lines, args=(logger, output), daemon=True).start()
    log = []
    try:
        run([*hdc, 'shell', 'aa', 'start', '-a', 'EntryAbility', '-b', BUNDLE,
             '--ps', 'mode', mode, '--ps', 'runId', run_id])
        deadline = time.monotonic() + args.timeout
        while time.monotonic() < deadline:
            try:
                line = output.get(timeout=min(1, max(.01, deadline - time.monotonic())))
            except queue.Empty:
                continue
            if line is None:
                raise RuntimeError('HDC log stream exited before a result')
            log.append(line)
            print(line, end='', flush=True)
            if run_id not in line:
                continue
            if 'AJNI_OHOS_MVP FAIL' in line:
                raise RuntimeError(f'{mode} scenario failed; inspect device log')
            if 'AJNI_OHOS_MVP PASS' in line:
                return log
        raise RuntimeError(f'Timed out waiting for {mode} result; no runtime pass claimed')
    finally:
        logger.terminate()
        try:
            logger.wait(timeout=5)
        except subprocess.TimeoutExpired:
            logger.kill()
            logger.wait()
        args.output.mkdir(parents=True, exist_ok=True)
        (args.output / f'{mode}.log').write_text(''.join(log))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--deveco', type=Path,
                        default=Path(os.environ.get('DEVECO_HOME', '/mnt/e/Apps/DevEco Studio')))
    parser.add_argument('--stage', type=Path,
                        default=Path(os.environ.get('AJNI_OHOS_STAGE', '/mnt/e/tmp/ajni-ohos-mvp')))
    parser.add_argument('--hap', type=Path)
    parser.add_argument('--target')
    parser.add_argument('--mode', choices=('suite', 'cleanup', 'both'), default='both')
    parser.add_argument('--timeout', type=int, default=90)
    parser.add_argument('--output', type=Path, default=Path('_build/ohos-device'))
    args = parser.parse_args()
    try:
        executable = args.deveco / 'sdk/default/openharmony/toolchains/hdc.exe'
        target_output = run([executable, 'list', 'targets']).stdout
        targets = [line.strip() for line in target_output.splitlines()
                   if line.strip() and line.strip() != '[Empty]']
        if args.target:
            if args.target not in targets:
                raise RuntimeError('Requested HDC target is not connected')
            target = args.target
        elif len(targets) == 1:
            target = targets[0]
        else:
            raise RuntimeError(f'Expected one HDC target, found {len(targets)}; use --target')
        hdc = [executable, '-t', target]
        if args.hap:
            hap = args.hap
        else:
            candidates = list(args.stage.glob('entry/build/**/outputs/**/*-signed.hap'))
            if len(candidates) != 1:
                raise RuntimeError('Expected one signed HAP; sign in DevEco or supply --hap')
            hap = candidates[0]
        install = run([*hdc, 'install', '-r', windows_path(hap)])
        if 'success' not in install.stdout.lower():
            raise RuntimeError('HDC did not report installation success; inspect signing/install error')
        identity = run([*hdc, 'shell', 'param', 'get', 'const.product.cpu.abilist']).stdout
        version = run([*hdc, 'shell', 'param', 'get', 'const.ohos.fullname']).stdout
        api = run([*hdc, 'shell', 'param', 'get', 'const.ohos.apiversion']).stdout
        # Run cleanup first in a fresh process. Suite must start with a fresh UI owner.
        modes = ['cleanup', 'suite'] if args.mode == 'both' else [args.mode]
        for mode in modes:
            scenario(hdc, mode, args)
        args.output.mkdir(parents=True, exist_ok=True)
        (args.output / 'device.json').write_text(json.dumps({
            'target': target, 'abi': identity.strip(), 'os': version.strip(),
            'api': api.strip(), 'modes': modes, 'hap': str(hap.resolve()),
        }, indent=2) + '\n')
        print('Real Harmony device scenarios passed:', ', '.join(modes))
        return 0
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(error, file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
