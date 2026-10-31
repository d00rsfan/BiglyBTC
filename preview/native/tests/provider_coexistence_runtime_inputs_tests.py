"""Exact coexistence role/source input refusals; no build, profile or socket IO."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('manifest', 'provider', 'include-root', 'approval', 'private-approval'):
        parser.add_argument('--' + name, required=True, type=Path)
    parser.add_argument('--approval-sha256', required=True)
    args = parser.parse_args()
    binding = Path(__file__).resolve().parents[1] / 'engine/runtime-binding'
    verifier = binding / 'verify_runtime_manifest_coexistence.py'
    assert digest(args.approval) == args.approval_sha256 == '3407d518e13a86f2a64b3d748cbe0870ef7df57712fade48cb72c87d2962ad75'
    original = {p.resolve(): digest(p) for p in (args.manifest, args.provider, args.approval, args.private_approval)}
    cases = ['exact-approved', 'approval-proposal', 'wrong-approval-digest', 'wrong-artifact-approval',
             'wrong-private-approval', 'self-approved-manifest', 'old-public-library', 'old-public-sdk']
    for case in cases:
        with tempfile.TemporaryDirectory(prefix='biglybtc-coexistence-input-') as name:
            root = Path(name)
            manifest, library, headers = args.manifest, args.provider, args.include_root
            approval, approval_sha, private = args.approval, args.approval_sha256, args.private_approval
            if case in ('approval-proposal', 'wrong-artifact-approval'):
                value = json.loads(approval.read_bytes())
                if case == 'approval-proposal':
                    value['status'] = 'PROPOSAL_NOT_APPROVED'
                else:
                    value['artifact_sha256'] = '0' * 64
                approval = root / 'approval.json'
                approval.write_text(json.dumps(value) + '\n')
                approval_sha = digest(approval)
            elif case == 'wrong-approval-digest':
                approval_sha = '0' * 64
            elif case == 'wrong-private-approval':
                private = approval
            elif case == 'self-approved-manifest':
                value = json.loads(manifest.read_bytes())
                value['native_coexistence_qualification_pending'] = False
                manifest = root / 'manifest.json'
                manifest.write_text(json.dumps(value) + '\n')
            elif case in ('old-public-library', 'old-public-sdk'):
                old = json.loads((binding / 'provider-mainline-routing-runtime.json').read_bytes())
                if case == 'old-public-library':
                    library = Path(old['artifact'])
                else:
                    headers = Path(old['install_root']) / 'include'
            result = subprocess.run([sys.executable, '-B', str(verifier), '--manifest', str(manifest),
                '--library', str(library), '--include', str(headers), '--approval', str(approval),
                '--approval-sha256', approval_sha, '--private-approval', str(private),
                '--header', str(root / 'generated.hpp'), '--receipt', str(root / 'generated.json')],
                capture_output=True, timeout=45)
            if case == 'exact-approved':
                assert result.returncode == 0, (case, result.stderr.decode(errors='replace'))
                receipt = json.loads((root / 'generated.json').read_bytes())
                assert receipt['same_image_exports'] == 34 and receipt['approval_sha256'] == approval_sha
                assert receipt['native_coexistence_qualification_pending'] is True
                assert '#define BIGLYBTC_PERSISTENT_COEXISTENCE_BOUND 1' in (root / 'generated.hpp').read_text()
            else:
                assert result.returncode != 0, case
                assert not (root / 'generated.hpp').exists() and not (root / 'generated.json').exists(), case
            print('PASS coexistence-input ' + case)
    assert all(digest(path) == expected for path, expected in original.items()), 'Retained inputs changed'


if __name__ == '__main__':
    main()
