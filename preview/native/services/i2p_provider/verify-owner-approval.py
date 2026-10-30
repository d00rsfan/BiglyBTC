#!/usr/bin/env python3
"""Configure-only private artifact closure. Never builds or runs the provider."""
import hashlib
import json
from pathlib import Path
import re
import sys


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        while data := stream.read(65536):
            h.update(data)
    return h.hexdigest()


def main():
    approval_path, approval_sha, artifact_path, sdk_path, expected_source, output = sys.argv[1:]
    approval_path, artifact, sdk = map(Path, (approval_path, artifact_path, sdk_path))
    assert approval_path.is_absolute() and artifact.is_absolute() and sdk.is_absolute()
    assert digest(approval_path) == approval_sha
    approval = json.loads(approval_path.read_text())
    assert approval['schema'] == 'BIGLYBTC_PRIVATE_I2P_OWNER_APPROVAL_V1'
    for field in ('source_capsule_sha256', 'artifact_sha256', 'sdk_tree_sha256',
                  'runtime_manifest_sha256', 'source_build_record_sha256', 'review_sha256'):
        assert re.fullmatch('[0-9a-f]{64}', approval[field])
    assert approval['source_capsule_sha256'] == expected_source
    assert artifact.is_file() and not artifact.is_symlink()
    assert 0 < approval['artifact_bytes'] <= 64 * 1024 * 1024
    assert artifact.stat().st_size == approval['artifact_bytes'] and digest(artifact) == approval['artifact_sha256']
    expected = approval['headers_sha256']
    assert 200 <= len(expected) <= 2048
    actual = {str(p.relative_to(sdk)): digest(p) for p in sorted((sdk / 'libtorrent').rglob('*')) if p.is_file()}
    assert actual == expected and all(not (sdk / name).is_symlink() for name in actual)
    tree = hashlib.sha256(json.dumps(actual, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
    assert tree == approval['sdk_tree_sha256']
    assert 'libtorrent/biglybtc_private_i2p_owner.hpp' in actual
    # No runtime discovery/allowlist extension: these exact constants are
    # compiled only for the explicitly requested new private target.
    text = '#pragma once\nnamespace biglybtc::services::i2p::provider::private_i2p_runtime_binding {\n'
    text += f'inline constexpr char sha256[]="{approval["artifact_sha256"]}";\n'
    text += f'inline constexpr unsigned long long bytes={approval["artifact_bytes"]}ULL;\n'
    for name in ('source_capsule_sha256', 'sdk_tree_sha256', 'runtime_manifest_sha256', 'source_build_record_sha256', 'review_sha256'):
        text += f'inline constexpr char {name}[]="{approval[name]}";\n'
    text += f'inline constexpr char approval_sha256[]="{approval_sha}";\n}}\n'
    Path(output).write_text(text)


if __name__ == '__main__':
    main()
