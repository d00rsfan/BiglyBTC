#!/usr/bin/env python3
"""Root-run exact metadata export only; no Git mutation/build/subject execution."""
from pathlib import Path
import argparse
import hashlib
import json
import shutil
import subprocess

HERE = Path(__file__).resolve().parent
W = HERE.parent
R = W.parents[1]
N = R.parent / 'BiglyBTC'
SOURCE = '471ebb75fc66fefb0208b0d086a934b68aced84b'
E_REL = 'migration/evidence/20261025-compatibility-providers'


def require(value, message):
    if not value:
        raise ValueError(message)


def sha(path):
    require(path.is_file() and not path.is_symlink() and path.resolve(strict=True) == path,
            'Unsafe metadata source/target: ' + str(path))
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--write', action='store_true')
    args = parser.parse_args()
    plan = json.loads((W / 'current-metadata-plan.json').read_text())
    require(plan['source'] == plan['parent'] == SOURCE and plan['evidence'] == E_REL,
            'Wrong metadata export parent')
    require(subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=N, text=True).strip() == SOURCE and
            not subprocess.check_output(['git', 'status', '--porcelain', '--untracked-files=all'],
                                        cwd=N, text=True).strip(), 'Require clean471 before export')
    require(not (N / E_REL).exists(), 'Never overwrite prior evidence')
    allowed = set(plan['tracking_files']) | {'README.md'}
    for rel, row in plan['exports'].items():
        key = Path(rel)
        require(not key.is_absolute() and '..' not in key.parts and
                (rel in allowed or rel.startswith(E_REL + '/')), 'Unexpected metadata export target')
        origin, target = Path(row['source']), N / rel
        require(origin.is_absolute() and origin.is_relative_to(R) and sha(origin) == row['sha256'],
                'Metadata source changed: ' + rel)
        require(not target.is_symlink() and
                (not target.exists() if row['before_sha256'] is None else
                 sha(target) == row['before_sha256']), 'Metadata target changed: ' + rel)
    if not args.write:
        print(json.dumps({'status': 'EXACT_METADATA_EXPORT_READY', 'source': SOURCE,
                          'files': len(plan['exports'])}))
        return
    for rel, row in plan['exports'].items():
        target = N / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(Path(row['source']), target)
        require(sha(target) == row['sha256'], 'Exported bytes differ: ' + rel)
    receipt = {'status': 'EXACT_METADATA_EXPORTED_NOT_COMMITTED', 'source': SOURCE,
               'production_source_not_modified': True, 'plan_sha256': sha(W / 'current-metadata-plan.json'),
               'files_sha256': {rel: sha(N / rel) for rel in plan['exports']}}
    (W / 'current-metadata-export.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps({'status': receipt['status'], 'files': len(plan['exports'])}))


if __name__ == '__main__':
    main()
