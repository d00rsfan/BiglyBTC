#!/usr/bin/env python3
"""Update the existing optional preview only, after final root qualification."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

R = Path('/home/yurii/Java/BiglyBT')
N = R.parent / 'BiglyBTC'
T = R / 'build/coexist-20261031'
CACHE = R / 'build/i2p-provider-attachment-20261029/native-followup-20261030/caches/gcc'
ROLE_SHA = '3407d518e13a86f2a64b3d748cbe0870ef7df57712fade48cb72c87d2962ad75'
PRIVATE_SHA = 'f3ca7de94ca6beb12e0375293c9c74c3f78a34f7e4952e1a122ff0ae9dff1f83'
MANIFEST_SHA = '62f7545f96af46806e7a7708213319ffd1e91c7a5228f0c35065eaa249a91343'
PROVIDER_SHA = '7ffbd4c63d7770168bcbdf8e8c19644e18d1509d57ac148331a3c4bd094310c4'
DEFAULT_SHA = 'aad928753cb8f2dac8f99245298d987cfe2c4bee9e7bdebf5b8d7645ed6d559b'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def git(*args):
    return subprocess.check_output(['git', '-C', str(N), *args], text=True).strip()


def inventory(root):
    for path in root.rglob('*'):
        if path.is_symlink():
            assert path.resolve(strict=True).is_relative_to(root), path
    return {p.relative_to(root).as_posix(): sha(p) for p in sorted(root.rglob('*'))
            if p.is_file() and p != root / 'BUILD_MANIFEST.json'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-commit', required=True)
    parser.add_argument('--build-receipt', required=True, type=Path)
    parser.add_argument('--build-receipt-sha256', required=True)
    parser.add_argument('--previous-manifest-sha256', required=True)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    assert re.fullmatch('[0-9a-f]{40}', args.source_commit)
    assert git('rev-parse', 'HEAD') == args.source_commit
    assert not git('status', '--porcelain', '--untracked-files=all', '--', 'preview/native')
    receipt = json.loads(args.build_receipt.read_bytes())
    assert sha(args.build_receipt) == args.build_receipt_sha256
    assert receipt['status'] == 'PASS' and receipt['lane'] == 'private'
    assert receipt['source_commit'] == args.source_commit
    assert receipt['source_tree'] == git('rev-parse', 'HEAD:preview/native')
    assert Path(receipt['cache']).resolve(strict=True) == CACHE
    source = args.build_receipt.parent / 'source-before.json'
    assert sha(source) == receipt['source_map_sha256']
    saved = json.loads(source.read_bytes())
    assert set(saved) == set(git('ls-tree', '-r', '--name-only', 'HEAD', '--', 'preview/native').splitlines())
    assert all(sha(N / name) == digest for name, digest in saved.items())
    for command in receipt['commands']:
        assert command['exit_code'] == 0 and not command.get('timed_out', False)
        assert sha(Path(command['log'])) == command['log_sha256']
    images = receipt['images']
    for target, path in [('BiglyBTC', CACHE / 'app/BiglyBTC'),
                         ('biglybtc-runtime-binding-driver', CACHE / 'tests/biglybtc-runtime-binding-driver')]:
        assert Path(images[target]['path']) == path
        assert sha(path) == images[target]['sha256'] and path.stat().st_size == images[target]['bytes']
    default, dest = N / 'dist', N / 'dist-i2p'
    assert dest.is_dir() and not dest.is_symlink() and dest.resolve() == dest
    assert sha(default / 'BUILD_MANIFEST.json') == DEFAULT_SHA
    default_before = inventory(default)
    prior_path = dest / 'BUILD_MANIFEST.json'
    assert sha(prior_path) == args.previous_manifest_sha256
    prior = json.loads(prior_path.read_bytes())
    before = inventory(dest)
    assert before == prior['files_sha256'] and len(before) == 119
    assert all(value == PROVIDER_SHA for name, value in before.items() if 'libtorrent-rasterbar.so' in name)
    doc = dest / 'usr/share/doc/biglybtc-experimental/private-i2p'
    assert sha(doc / 'root-private-owner-approval.json') == PRIVATE_SHA
    runtime_path = T / 'sdk-role-integration/runtime-manifest.json'
    role_path = T / 'sdk-role-integration/root-coexistence-approval.json'
    assert sha(runtime_path) == MANIFEST_SHA and sha(role_path) == ROLE_SHA
    runtime = json.loads(runtime_path.read_bytes())
    assert sha(doc / 'source-build-record.json') == runtime['source_build_record_sha256']
    private_path = R / 'build/i2p-multi-20261030/provider/root-private-owner-approval.json'
    assert sha(private_path) == PRIVATE_SHA
    args.output = args.output.resolve()
    assert args.output.is_relative_to(T) and not args.output.exists()
    args.output.mkdir(parents=True)
    binding = N / 'preview/native/engine/runtime-binding'
    subprocess.run([sys.executable, '-B', str(binding / 'verify_runtime_manifest_coexistence.py'),
        '--manifest', str(runtime_path), '--library', runtime['artifact'],
        '--include', str(Path(runtime['artifact']).parents[1] / 'include'),
        '--approval', str(role_path), '--approval-sha256', ROLE_SHA, '--private-approval', str(private_path),
        '--header', str(args.output / 'binding.hpp'), '--receipt', str(args.output / 'binding.json')], check=True, timeout=60)
    assert (args.output / 'binding.hpp').read_bytes() == (CACHE / 'engine/runtime-binding/persistent_runtime_config.hpp').read_bytes()
    for name in ['BiglyBTC-plugin-azlocprov.so', 'BiglyBTC-plugin-SpeedScheduler.so']:
        assert sha(CACHE / 'app' / name) == sha(dest / 'usr/lib/biglybtc/lib' / name), 'Changed plugin needs separate staging review'
    environment = {k: v for k, v in os.environ.items() if k not in ('LD_PRELOAD', 'LD_AUDIT', 'LD_LIBRARY_PATH')}
    lookup = [dest / 'usr/lib/biglybtc/lib', dest / 'usr/lib/biglybtc/qt/lib',
              dest / 'dependencies/libtorrent/usr/lib/x86_64-linux-gnu']
    environment['LD_LIBRARY_PATH'] = os.pathsep.join(map(str, lookup))
    linked = subprocess.check_output(['ldd', str(CACHE / 'app/BiglyBTC')], env=environment, text=True, timeout=30)
    assert all(x not in linked for x in ('not found', '/opt/Qt/', '/tmp/', str(R / 'build'))), linked
    for name in ('libtorrent-rasterbar.so.2.0', 'libQt6Core.so.6', 'libQt6Network.so.6', 'libQt6Gui.so.6', 'libQt6Widgets.so.6'):
        row = next(line for line in linked.splitlines() if line.strip().startswith(name + ' => '))
        assert Path(row.split('=>', 1)[1].split()[0]).resolve().is_relative_to(dest), row
    (args.output / 'ldd.log').write_text(linked)
    # Only two small copied artifacts: genuine relocated guard/34-export smoke.
    with tempfile.TemporaryDirectory(prefix='relocation-', dir=args.output) as temporary:
        moved = Path(temporary)
        shutil.copy2(CACHE / 'tests/biglybtc-runtime-binding-driver', moved / 'binding-driver')
        shutil.copy2(Path(runtime['artifact']), moved / 'libtorrent-rasterbar.so.2.0.12')
        (moved / 'libtorrent-rasterbar.so.2.0').symlink_to('libtorrent-rasterbar.so.2.0.12')
        relocated = dict(environment, LD_LIBRARY_PATH=str(moved) + os.pathsep + environment['LD_LIBRARY_PATH'])
        smoke = subprocess.run([str(moved / 'binding-driver'), 'bound'], env=relocated, capture_output=True, timeout=15)
        (args.output / 'relocation.stdout').write_bytes(smoke.stdout)
        (args.output / 'relocation.stderr').write_bytes(smoke.stderr)
        assert smoke.returncode == 0, smoke.stderr
    changed = {'usr/lib/biglybtc/bin/BiglyBTC', 'README.txt', 'usr/share/doc/biglybtc-experimental/private-i2p/SCOPE.md'}
    texts = {
        'README.txt': 'Optional Public + I2P desktop preview.\n\nRun ./BiglyBTC --profile ABSOLUTE_ISOLATED_PROFILE --i2p-router OWNED_I2CP_PORT for separate Public and private I2P sessions with one combined saved queue/rate budget. Existing --private-i2p retains private-only startup. I2P torrents require genuine saved pure-I2P metadata, source settings and original key; none are bundled. Each torrent remains confined to its selected network.\n\nThis is a bounded optional preview, not complete plugin/backend/performance parity. Mixed Speed/ETA download probes use the explicit unavailable/position fallback. Keep a backup of your profile before testing.\n',
        'usr/share/doc/biglybtc-experimental/private-i2p/SCOPE.md': 'Optional separately approved same-image Public/private sessions; shared saved queue, finite global bandwidth allocation, independently controlled pure-network torrents and checked cumulative library observations. Existing private-only mode retained. General I2P discovery/multihop, full plugin parity and performance guarantees remain open. Mixed Speed/ETA probes remain explicitly unavailable. No user profile, identity keys or ephemeral owner capability is included.\n'}
    for name in sorted(changed):
        path = dest / name
        temporary = path.with_name(path.name + '.coexist-stage')
        assert not temporary.exists()
        if name.endswith('/BiglyBTC'):
            shutil.copy2(CACHE / 'app/BiglyBTC', temporary)
        else:
            temporary.write_text(texts[name])
        os.replace(temporary, path)
    after = inventory(dest)
    assert set(after) == set(before) and len(after) == 119
    assert after['usr/lib/biglybtc/bin/BiglyBTC'] == images['BiglyBTC']['sha256']
    assert all(after[name] == digest for name, digest in before.items() if name not in changed)
    manifest = {'scope': 'Optional separate Public/private sessions; shared saved queue/rates; no mixed-network torrents',
        'source_commit': args.source_commit, 'source_tree': receipt['source_tree'],
        'build_receipt_sha256': args.build_receipt_sha256, 'previous_manifest_sha256': args.previous_manifest_sha256,
        'launch': './BiglyBTC --profile ABSOLUTE_ISOLATED_PROFILE --i2p-router OWNED_I2CP_PORT',
        'private_only_launch': './BiglyBTC --profile ABSOLUTE_ISOLATED_PROFILE --private-i2p OWNED_I2CP_PORT',
        'provider_sha256': PROVIDER_SHA, 'provider_source_capsule_sha256': runtime['source_capsule_sha256'],
        'role_approval_sha256': ROLE_SHA, 'role_approval_raw': role_path.read_text(),
        'runtime_manifest_sha256': MANIFEST_SHA, 'runtime_manifest_raw': runtime_path.read_text(),
        'private_approval_sha256': PRIVATE_SHA, 'default_dist_preserved_sha256': DEFAULT_SHA,
        'files_sha256': after, 'shipping_approved': False}
    temporary = prior_path.with_name('BUILD_MANIFEST.json.coexist-stage')
    temporary.write_text(json.dumps(manifest, indent=2) + '\n')
    os.replace(temporary, prior_path)
    assert inventory(default) == default_before and sha(default / 'BUILD_MANIFEST.json') == DEFAULT_SHA
    assert all(sha(N / name) == digest for name, digest in saved.items())
    assert git('rev-parse', 'HEAD') == args.source_commit
    result = {'status': 'PASS_STAGED_OPTIONAL_COEXISTENCE_PREVIEW', 'source_commit': args.source_commit,
        'manifest_sha256': sha(prior_path), 'mapped_files': 119, 'default_dist_unchanged': True,
        'build_receipt_sha256': args.build_receipt_sha256, 'role_approval_sha256': ROLE_SHA,
        'relocation_exit': smoke.returncode, 'ldd_sha256': sha(args.output / 'ldd.log')}
    (args.output / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
