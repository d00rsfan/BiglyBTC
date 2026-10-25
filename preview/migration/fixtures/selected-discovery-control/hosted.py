#!/usr/bin/env python3
"""Closed-input hosted recipe. Deliberately blocked until root freezes a catalog.

This is NOT an approval producer. A changed/new artifact or catalog rejects.
Only GitHub's ordinary authenticated artifact downloader performs network IO.
The actual DHT/LSD fixture retains its separate private-namespace gate.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import stat
import subprocess
import sys
import zipfile

sys.dont_write_bytecode = True
CATALOG_SHA256 = 'cfd847973aa36c3325495feca36b2cfa59a2b0a022f8b8e12ddf524ee664551c'
CHAPTER = '/home/yurii/Java/BiglyBT/build/storage-20261025'
CAPSULE_SHA = 'f5c55c2dde4bba885f4e3fa1dbc81e3659e49880c0856797eba156703d692b2f'
PROVIDER_SHA = 'e74def09b2257a266786b8ddb116382d300716687bf26bbe57072f5ff90e8737'
PROVIDER_IMAGE_SHA = 'd367afd6499859c5a57d66bddd352a977ff76c7fb76b5925302c2c9008c7ddbd'
PROVIDER_IMAGE = Path(CHAPTER) / 'provider-candidate/install/lib/libtorrent-rasterbar.so.2.0.12'
# Exactly the two original LT install aliases, in target-first creation order.
RESTORED_ALIASES = {
    str(PROVIDER_IMAGE.with_name('libtorrent-rasterbar.so.2.0')).lstrip('/'): 'libtorrent-rasterbar.so.2.0.12',
    str(PROVIDER_IMAGE.with_name('libtorrent-rasterbar.so')).lstrip('/'): 'libtorrent-rasterbar.so.2.0',
}


def need(ok, why):
    if not ok:
        raise ValueError(why)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def reviewed_aliases(value):
    need(value['aliases_dereferenced_as_identical_regular_files'] == sorted(RESTORED_ALIASES),
         'only the exact two catalog-pinned LT SONAME aliases may be restored')
    for name in (*RESTORED_ALIASES, str(PROVIDER_IMAGE).lstrip('/')):
        need(value['members_sha256'].get(name) == PROVIDER_IMAGE_SHA,
             'alias/target bytes differ from the exact reviewed provider')
    need(value['runner_options']['artifact'] == str(PROVIDER_IMAGE)
         and value['runner_options']['artifact-sha256'] == PROVIDER_IMAGE_SHA,
         'alias target differs from the actual runtime artifact')


def verify_restored_aliases(value):
    reviewed_aliases(value)
    need(PROVIDER_IMAGE.is_file() and not PROVIDER_IMAGE.is_symlink()
         and PROVIDER_IMAGE.resolve(strict=True) == PROVIDER_IMAGE
         and digest(PROVIDER_IMAGE) == PROVIDER_IMAGE_SHA, 'changed/noncanonical provider target')
    target = PROVIDER_IMAGE.stat()
    for name, link_text in RESTORED_ALIASES.items():
        path = Path('/') / name
        need(path.is_symlink() and os.readlink(path) == link_text
             and path.parent.resolve(strict=True) == PROVIDER_IMAGE.parent
             and path.resolve(strict=True) == PROVIDER_IMAGE, 'changed/nonexact LT SONAME alias')
        actual = path.stat()
        need((actual.st_dev, actual.st_ino) == (target.st_dev, target.st_ino)
             and digest(path) == value['members_sha256'][name], 'LT alias does not identify the exact provider')


def catalog(path):
    need(re.fullmatch('[0-9a-f]{64}', CATALOG_SHA256) is not None,
         'Root must review and source-freeze ONE exact hosted catalog first')
    need(path.is_file() and not path.is_symlink() and path.resolve(strict=True) == path
         and digest(path) == CATALOG_SHA256, 'changed/unapproved hosted catalog')
    value = json.loads(path.read_bytes())
    need(value['schema'] == 1 and value['status'] == 'ROOT_REVIEWED_EXACT_PROVIDER_INPUTS', 'unapproved catalog state')
    need(type(value['run_id']) is int and value['run_id'] > 0, 'exact actual artifact-producing run required')
    need(re.fullmatch('[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+', value['repository']) is not None, 'exact repository')
    need(re.fullmatch('[A-Za-z0-9_.-]+', value['artifact_name']) is not None, 'exact artifact name')
    need(re.fullmatch('[0-9a-f]{64}', value['bundle_sha256']) is not None, 'exact reviewed bundle hash required')
    need(value['members_sha256'] and type(value['bundle_bytes']) is int and value['bundle_bytes'] > 0,
         'complete frozen bundle member map/size required')
    for name, pin in value['members_sha256'].items():
        relative = PurePosixPath(name)
        need(not relative.is_absolute() and str(relative) == name and '..' not in relative.parts
             and re.fullmatch('[0-9a-f]{64}', pin) is not None, 'unsafe/unpinned bundle member')
    need(value['source_capsule_sha256'] == CAPSULE_SHA and value['provider_manifest_sha256'] == PROVIDER_SHA,
         'wrong frozen source/provider cohort')
    need(type(value['artifact_id']) is int and value['artifact_id'] > 0
         and re.fullmatch('sha256:[0-9a-f]{64}', value['artifact_digest']) is not None,
         'actual GitHub staging artifact identity/digest required')
    need(re.fullmatch('[0-9a-f]{40}', value['staging_head_sha']) is not None,
         'actual staging source commit required')
    need(value['restore_roots'] == [CHAPTER], 'exact chapter restore root required')
    for root in value['restore_roots']:
        # Original qualification paths must be restored exactly, without a
        # relocation waiver. Only root-reviewed owned build/artifact areas.
        need(root.startswith(('/home/yurii/Java/BiglyBT/build/', '/home/yurii/Java/BiglyBTC/preview/native/engine/runtime-binding/',
                              '/tmp/biglybtc-deps/', '/tmp/biglybtc-toolchains/'))
             and '..' not in Path(root).parts, 'restore root outside reviewed owned artifact areas')
    reviewed_aliases(value)
    return value


def inspect_bundle(path, value):
    need(path.is_file() and not path.is_symlink() and digest(path) == value['bundle_sha256']
         and path.stat().st_size == value['bundle_bytes'], 'changed hosted bundle')
    with zipfile.ZipFile(path) as archive:
        files = archive.infolist()
        need(len(files) == len(value['members_sha256']) and len(files) <= 10000, 'unexpected bundle cohort')
        need(len({row.filename for row in files}) == len(files), 'duplicate ZIP name')
        total = 0
        for row in files:
            need(not row.is_dir() and row.filename in value['members_sha256'], 'unapproved bundle path')
            mode = row.external_attr >> 16
            need(not stat.S_ISLNK(mode) and (stat.S_IFMT(mode) in (0, stat.S_IFREG)), 'special/link bundle member')
            target = Path('/') / row.filename
            need(any(target == Path(root) or Path(root) in target.parents for root in value['restore_roots']),
                 'bundle entry outside exact restore roots')
            need(row.file_size <= 256 * 1024 * 1024 and row.compress_size > 0, 'oversized bundle leaf')
            total += row.file_size
            need(total <= 2 * 1024 * 1024 * 1024, 'oversized bundle')
            need(hashlib.sha256(archive.read(row)).hexdigest() == value['members_sha256'][row.filename], 'changed bundle member')


def restore(bundle, value):
    need(os.geteuid() == 0, 'hosted restoration requires the standard runner sudo boundary')
    inspect_bundle(bundle, value)
    # Complete preflight before any destination mutation. Never overwrite a
    # prior tree, symlink or root-owned system file.
    for name in value['members_sha256']:
        target = Path('/') / name
        need(target.resolve() == target and not target.exists() and not target.is_symlink(), 'restore destination exists/aliases')
    with zipfile.ZipFile(bundle) as archive:
        for row in archive.infolist():
            if row.filename in RESTORED_ALIASES:
                continue  # ZIP bytes already verified; restore the original link identity below.
            target = Path('/') / row.filename
            target.parent.mkdir(parents=True, exist_ok=True)
            with target.open('xb') as stream:
                stream.write(archive.read(row))
            os.chmod(target, 0o755 if (row.external_attr >> 16) & 0o111 else 0o644)
    need(PROVIDER_IMAGE.is_file() and not PROVIDER_IMAGE.is_symlink()
         and PROVIDER_IMAGE.resolve(strict=True) == PROVIDER_IMAGE
         and digest(PROVIDER_IMAGE) == PROVIDER_IMAGE_SHA, 'changed canonical alias target')
    for name, link_text in RESTORED_ALIASES.items():
        target = Path('/') / name
        need(not target.exists() and not target.is_symlink(), 'alias destination already exists')
        os.symlink(link_text, target)  # No overwrite, no caller-selected link or target.
    verify_restored_aliases(value)


def execute(value):
    need(os.geteuid() == 0, 'hosted fixture runs through standard runner sudo')
    verify_restored_aliases(value)
    for name, pin in value['members_sha256'].items():
        path = Path('/') / name
        if name in RESTORED_ALIASES:
            continue  # Exact link text, canonical inode and bytes were verified above.
        need(path.is_file() and not path.is_symlink() and path.resolve(strict=True) == path
             and digest(path) == pin, 'restored qualification input differs')
    options = value['runner_options']
    expected = {'driver', 'driver-sha256', 'encoder', 'encoder-sha256', 'artifact', 'artifact-sha256',
                'runtime-manifest', 'approver', 'approver-sha256', 'approver-module-sha256', 'output'}
    need(set(options) == expected, 'closed runtime option cohort required')
    runner = Path(value['runner'])
    need(str(runner).lstrip('/') in value['members_sha256'], 'runner not in reviewed bundle')
    command = [sys.executable, str(runner)]
    for key in sorted(options):
        command.extend(['--' + key, str(options[key])])
    # No token, loader injection or optional unreviewed runtime path passes to
    # qualification. The bundle must be ABI-compatible with this hosted image.
    environment = {key: value for key, value in os.environ.items()
                   if key not in ('GITHUB_TOKEN', 'GH_TOKEN', 'LD_PRELOAD', 'LD_AUDIT', 'LD_LIBRARY_PATH', 'PYTHONPATH', 'PYTHONHOME')}
    return subprocess.run(command, env=environment, check=False).returncode


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--catalog', required=True, type=Path)
    parser.add_argument('--bundle', required=True, type=Path)
    parser.add_argument('--restore', action='store_true')
    parser.add_argument('--execute', action='store_true')
    args = parser.parse_args()
    value = catalog(args.catalog)
    if args.restore:
        restore(args.bundle, value)
        return 0
    if args.execute:
        inspect_bundle(args.bundle, value)
        return execute(value)
    need(not args.bundle.exists() and not args.bundle.parent.exists(), 'new owned download directory required')
    need(os.environ.get('GITHUB_REPOSITORY') == value['repository'], 'wrong hosted repository')
    # Bind the actual successful upload artifact, not only a run/name string.
    actual = json.loads(subprocess.check_output(
        ['gh', 'api', 'repos/' + value['repository'] + '/actions/artifacts/' + str(value['artifact_id'])], timeout=60))
    need(actual['id'] == value['artifact_id'] and actual['name'] == value['artifact_name']
         and actual['expired'] is False and actual['digest'] == value['artifact_digest']
         and actual['workflow_run']['id'] == value['run_id'], 'changed/expired staging artifact')
    run = json.loads(subprocess.check_output(
        ['gh', 'api', 'repos/' + value['repository'] + '/actions/runs/' + str(value['run_id'])], timeout=60))
    need(run['id'] == value['run_id'] and run['status'] == 'completed' and run['conclusion'] == 'success'
         and run['head_sha'] == value['staging_head_sha'],
         'staging upload run must actually succeed')
    args.bundle.parent.mkdir(parents=True)
    subprocess.run(['gh', 'run', 'download', str(value['run_id']), '--repo', value['repository'],
                    '--name', value['artifact_name'], '--dir', str(args.bundle.parent)], check=True, timeout=300)
    need(list(args.bundle.parent.iterdir()) == [args.bundle], 'artifact must contain only the frozen bundle ZIP')
    inspect_bundle(args.bundle, value)
    for operation in ('--restore', '--execute'):
        command = ['sudo', '--', sys.executable, str(Path(__file__).resolve()), '--catalog', str(args.catalog),
                   '--bundle', str(args.bundle), operation]
        result = subprocess.run(command, check=False).returncode
        need(result == 0, 'isolated hosted qualification rejected or runtime gap; see retained evidence')
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except Exception as error:
        print('FAIL: ' + str(error), file=sys.stderr)
        raise SystemExit(2)
