#!/usr/bin/env python3
"""Promote one qualified candidate, retaining/restoring the entire old dist."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

NATIVE = Path('/home/yurii/Java/BiglyBTC')

def verify(directory):
    raw = directory / 'BUILD_MANIFEST.json'; data = json.loads(raw.read_text())
    for name, expected in data['files_sha256'].items():
        path = directory / name
        assert not Path(name).is_absolute() and '..' not in Path(name).parts
        assert path.resolve(strict=True).is_relative_to(directory.resolve()) and path.is_file()
        assert hashlib.sha256(path.read_bytes()).hexdigest() == expected, name
    actual = {str(p.relative_to(directory)) for p in directory.rglob('*') if p.is_file()}
    assert actual == set(data['files_sha256']) | {'BUILD_MANIFEST.json'}
    return hashlib.sha256(raw.read_bytes()).hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__); parser.add_argument('--attempt', required=True, type=Path)
    args = parser.parse_args(); publication = args.attempt.resolve(strict=True)
    assert publication.is_relative_to(NATIVE / 'build/storage-20261025/portable')
    result = json.loads((publication / 'verification/result.json').read_text())
    assert result['status'] == 'PRIVATE_RUNTIME_QUALIFIED_NOT_PROMOTED' and not result['promoted']
    assert not result['system_installation'] and not result['original_profile_or_process_access']
    assert subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=NATIVE, text=True).strip() == result['native_source']
    expected = json.loads((publication / 'verification/source-map.json').read_text())
    assert hashlib.sha256((publication / 'verification/source-map.json').read_bytes()).hexdigest() == result['source_map_sha256']
    assert {str(v.relative_to(NATIVE)) for v in (NATIVE / 'preview/native').rglob('*') if v.is_file()} == set(expected)
    assert subprocess.run(['git', 'diff', '--quiet', result['native_source'], '--', 'preview/native'], cwd=NATIVE).returncode == 0
    for name, digest in expected.items():
        assert name.startswith('preview/native/') and '..' not in Path(name).parts
        assert hashlib.sha256((NATIVE / name).read_bytes()).hexdigest() == digest, name
    dist = NATIVE / 'dist'; candidate = publication / 'candidate'; rollback = publication / 'previous-dist'
    assert str(candidate) == result['candidate'] and not rollback.exists() and not rollback.is_symlink()
    assert dist.is_dir() and not dist.is_symlink() and candidate.is_dir() and not candidate.is_symlink()
    old, new = verify(dist), verify(candidate)
    assert old == result['previous_manifest_sha256'] and new == result['manifest_sha256']
    dist.rename(rollback)
    try:
        candidate.rename(dist)
        assert verify(dist) == new and verify(rollback) == old
        receipt = {'native_source': result['native_source'], 'dist': str(dist), 'rollback': str(rollback),
            'old_manifest_sha256': old, 'manifest_sha256': new, 'manifest_files': result['manifest_files'],
            'system_installation': False, 'original_profile_or_process_access': False}
        (publication / 'promotion.json').write_text(json.dumps(receipt, indent=2) + '\n')
    except BaseException:
        if dist.exists():
            dist.rename(candidate)
        rollback.rename(dist)
        assert verify(dist) == old
        raise
    print(json.dumps({'source': receipt['native_source'], 'manifest': new, 'rollback': str(rollback)}), flush=True)

if __name__ == '__main__':
    main()
