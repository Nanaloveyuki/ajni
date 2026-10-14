#!/usr/bin/env python3
"""Validate a stable release tag against moon.mod and extract its changelog notes."""
import argparse
from pathlib import Path
import re
import sys
import tomllib

ROOT = Path(__file__).resolve().parents[1]
VERSION = re.compile(r'(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)')


def release_notes(root, tag):
    module = tomllib.loads((root / 'moon.mod').read_text(encoding='utf-8'))
    version = module['version']
    if not isinstance(version, str) or not VERSION.fullmatch(version):
        raise ValueError('moon.mod version must be a stable X.Y.Z version')
    if tag != f'v{version}':
        raise ValueError(f'release tag {tag!r} does not match moon.mod v{version}')
    changelog = (root / 'CHANGELOG.md').read_text(encoding='utf-8')
    sections = re.split(r'^## +(.+?)\s*$', changelog, flags=re.MULTILINE)
    matches = [sections[index + 1].strip() for index in range(1, len(sections), 2)
               if sections[index] == version]
    if len(matches) != 1 or not matches[0]:
        raise ValueError(f'CHANGELOG.md must contain exactly one nonempty ## {version} section')
    return matches[0] + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tag', required=True, help='Stable Git tag, e.g. v0.2.6')
    parser.add_argument('--notes-file', type=Path, help='Write the matching release notes here')
    args = parser.parse_args()
    try:
        notes = release_notes(ROOT, args.tag)
        if args.notes_file:
            args.notes_file.write_text(notes, encoding='utf-8')
    except (OSError, ValueError, KeyError) as error:
        print(error, file=sys.stderr)
        return 1
    print(f'Release metadata gate passed: {args.tag}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
