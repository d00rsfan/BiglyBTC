#!/usr/bin/env python3
"""Emit a source-only relocation proposal; never copy SDKs or change approval."""
import argparse
import hashlib
import json
from pathlib import Path


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--destination', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    bundle = Path(__file__).resolve().parent
    task = bundle.parent
    output = args.output.resolve()
    assert output.is_relative_to(task) and not output.exists(), 'New task-local proposal only'
    destination = args.destination.resolve()
    assert destination.is_absolute() and not destination.is_relative_to(bundle)
    manifest_path = bundle / 'manifest.json'
    manifest = json.loads(manifest_path.read_bytes())
    copies = []
    for name, expected in manifest['artifacts_sha256'].items():
        rel = Path(name)
        assert not rel.is_absolute() and '..' not in rel.parts
        source = bundle / rel
        assert source.is_file() and not source.is_symlink() and digest(source) == expected
        copies.append({'source': str(source), 'destination': str(destination / rel), 'sha256': expected})
    copies.append({'source': str(manifest_path), 'destination': str(destination / 'manifest.json'),
                   'sha256': digest(manifest_path)})
    external = json.loads((bundle / 'external-inputs.json').read_bytes())
    record = json.loads((bundle / 'measured/source-build-record-discovery-final.json').read_bytes())
    proposal = {
        'schema': 'BIGLYBTC_SOURCE_EVIDENCE_RELOCATION_PROPOSAL_V1',
        'status': 'ROOT_REVIEW_REQUIRED_NO_RUNTIME_AUTHORITY',
        'public_text_copies': copies,
        'original_local_record': str(task / 'source-build-record-discovery-final.json'),
        'source_root': record['source_root'], 'install_root': record['install_root'],
        'artifact': record['artifact'], 'artifact_sha256': record['artifact_sha256'],
        'external_record_evidence_base': external['record_evidence_base'],
        'external_record_evidence_sha256': external['record_evidence_sha256'],
        'tested_images_sha256': external['tested_images_sha256'],
        'closed_runtime_paths_rewritten': False,
        'sdk_or_native_mutated': False, 'subjects_or_builds_replayed': False,
        'required_root_actions': [
            'Review/copy only the exact public text leaves; no SDK, key, object or payload copying is performed here.',
            'Preserve the current local source/install/images/raw evidence closure while existing closed pins refer to it.',
            'If moving active records, retain every relative evidence leaf and rewrite exact record/proof/log paths in a separately reviewed proposal.',
            'Reverify full1090/294/285 maps, same42 exports, actual9/23/6 proof and raw command hashes before any new root approval.',
            'Bind new record/runtime/approval hashes and literal Native verifier pins together; historical approvals confer no relocated authority.',
            'Reconfigure affected Native caches and run relevant deployment/relocation guards before publishing an active relocated runtime.',
        ],
    }
    output.write_text(json.dumps(proposal, indent=2) + '\n')
    print(json.dumps({'proposal': str(output), 'sha256': digest(output), 'copies_planned': len(copies)}))


if __name__ == '__main__':
    main()
