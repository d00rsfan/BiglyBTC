#!/usr/bin/env python3
"""Download one manually reviewed release asset; never execute its contents."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

SPEC_SHA256 = 'c396e38089eb298530e7bb31bb78d279eb6a2d06fbeb8bd0a671df5c801ecc30'


def need(ok, why):
    if not ok:
        raise ValueError(why)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--spec', required=True, type=Path)
    parser.add_argument('--directory', required=True, type=Path)
    args = parser.parse_args()
    need(re.fullmatch('[0-9a-f]{64}', SPEC_SHA256) is not None,
         'root must first review/source-freeze one exact release transport specification')
    need(args.spec.is_file() and not args.spec.is_symlink() and args.spec.resolve(strict=True) == args.spec
         and hashlib.sha256(args.spec.read_bytes()).hexdigest() == SPEC_SHA256, 'changed/unapproved transport spec')
    value = json.loads(args.spec.read_bytes())
    need(value['schema'] == 1 and value['status'] == 'ROOT_REVIEWED_EXACT_BUNDLE_TRANSPORT', 'unreviewed transport')
    need(re.fullmatch('[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+', value['repository']) is not None
         and os.environ.get('GITHUB_REPOSITORY') == value['repository'], 'wrong repository')
    need(re.fullmatch('[A-Za-z0-9_.-]+', value['release_tag']) is not None
         and not value['release_tag'].startswith('-') and value['asset_name'] == 'bundle.zip', 'exact release/asset required')
    need(re.fullmatch('[0-9a-f]{64}', value['bundle_sha256']) is not None
         and type(value['bundle_bytes']) is int and 0 < value['bundle_bytes'] <= 2 * 1024 ** 3, 'exact bundle identity required')
    need(args.directory.is_absolute() and args.directory.parent.resolve(strict=True) == args.directory.parent
         and not args.directory.exists() and not args.directory.is_symlink(), 'new owned download directory required')
    args.directory.mkdir()
    subprocess.run(['gh', 'release', 'download', value['release_tag'], '--repo', value['repository'],
                    '--pattern', 'bundle.zip', '--dir', str(args.directory)], check=True, timeout=300)
    bundle = args.directory / 'bundle.zip'
    need(list(args.directory.iterdir()) == [bundle] and bundle.is_file() and not bundle.is_symlink()
         and bundle.stat().st_size == value['bundle_bytes']
         and hashlib.sha256(bundle.read_bytes()).hexdigest() == value['bundle_sha256'], 'release bytes differ')
    print(json.dumps({'status': 'EXACT_REVIEWED_BUNDLE_DOWNLOADED_NO_EXECUTION',
                      'bundle_sha256': value['bundle_sha256'], 'bytes': value['bundle_bytes']}))


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        print('FAIL: ' + str(error), file=sys.stderr)
        raise SystemExit(2)
