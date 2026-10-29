#!/usr/bin/env python3
"""Root-only fixed admin namespace adoption; importing changes no files."""
import argparse
import copy
import importlib.util
import json
import pathlib
import re
import shutil
import subprocess

W = pathlib.Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('admin_batch', W / 'run-admin.py')
b = importlib.util.module_from_spec(spec)
spec.loader.exec_module(b)
s, R, N = b.s, b.R, b.N
SUPPORT = pathlib.Path('/home/yurii/Java/BiglyBT/build/i2p-streaming-window-followup-20261029/amendments/public-bound/corrections/admin-namespace-recipes/amendments/preserve-unchanged-mtimes/amendments/native-hosted-dependency-basis/support/manifest.json')
SUPPORT_SHA = '1990f6c5080bd3ba995a9943239ae467e0536c054032565bda1ecd440388a157'


def git(*args):
    return subprocess.check_output(['git', '-C', str(N), *args], text=True).strip()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--attempt', required=True)
    for name in ('namespace-preflight', 'parent-adoption', 'parent-reference'):
        parser.add_argument('--' + name, type=pathlib.Path, required=True)
    args = parser.parse_args()
    assert re.fullmatch('[A-Za-z0-9_-]+', args.attempt)
    authority = b.namespace_pins(args.namespace_preflight)
    parent = s.load(args.parent_adoption)
    assert git('rev-parse', 'HEAD') == parent['source_commit'] == 'ad4854e920d86a511cc90cd52e4ab161adcb0fec'
    assert git('branch', '--show-current') == 'main' and not git('status', '--porcelain')
    out = W / ('root-adoption-' + args.attempt + '.json')
    reference_out = W / 'java-runs' / args.attempt
    assert not out.exists() and not reference_out.exists()
    assert len(parent['source_paths']) == 26
    for name, row in parent['source_paths'].items():
        assert s.sha(N / name) == row['after_sha256']
    reference = b.reference_pins(args.parent_reference)
    assert reference['source_commit'] == parent['source_commit'] and reference['adoption_sha256'] == s.sha(args.parent_adoption)
    assert s.sha(b.PUBLIC) == b.PUBLIC_SHA and s.sha(b.PUBLIC_REVIEW) == b.PUBLIC_REVIEW_SHA
    capsule = s.load(b.PUBLIC)
    for name, expected in capsule['artifacts_sha256'].items():
        assert s.sha(b.PUBLIC.parent / name) == expected
    changes, maps = {}, copy.deepcopy(parent['source_paths'])
    for name, row in capsule['source_paths'].items():
        assert name.startswith(('preview/native/', '.github/workflows/')) and '..' not in pathlib.Path(name).parts
        assert (s.sha(N / name) if (N / name).exists() else None) == row['before_sha256']
        assert s.sha(row['after_path']) == row['after_sha256']
        changes[name] = pathlib.Path(row['after_path']).read_bytes()
        # Keep the original accepted cohort's beforeimage, update final bytes.
        maps[name] = {**maps.get(name, row), 'after_path': row['after_path'], 'after_sha256': row['after_sha256']}
    assert len(maps) == 31
    assert s.sha(SUPPORT) == SUPPORT_SHA
    support = s.load(SUPPORT)
    for name, expected in support['artifacts_sha256'].items():
        assert s.sha(SUPPORT.parent / name) == expected
    canonical = pathlib.Path(support['canonical_reference']['path'])
    assert s.sha(canonical) == support['canonical_reference']['sha256']
    native_support = support['support_path']
    native_after = pathlib.Path(support['after_path'])
    assert s.sha(native_after) == support['after_sha256']
    assert s.sha(N / native_support) == support['before_sha256']
    support_maps = {native_support: {'canonical_reference': support['canonical_reference'], 'before_path': str(SUPPORT.parent / 'before.sh'),
                                    'before_sha256': support['before_sha256'], 'after_path': str(native_after),
                                    'after_sha256': support['after_sha256']}}
    failures = {str(file): s.sha(file) for root in (W / 'runs').glob('*') if root.is_dir()
                for file in root.iterdir() if file.is_file() and file.suffix in ('.json', '.log', '.xml', '.txt')}
    admin = R / 'build/azureus-public-bound-20261029/admin-fixture'
    namespace_history = {str(file): s.sha(file) for directory in (admin / 'compiled', admin / 'preflight')
                         for root in directory.iterdir() if root.is_dir() for file in root.iterdir()
                         if file.is_file() and file.suffix in ('.json', '.log', '.txt', '.mk')}
    for name, data in changes.items():
        target = N / name
        target.parent.mkdir(parents=True, exist_ok=True)
        if not target.exists() or target.read_bytes() != data:
            target.write_bytes(data)
    if (N / native_support).read_bytes() != native_after.read_bytes():
        (N / native_support).write_bytes(native_after.read_bytes())
    for name, row in {**maps, **support_maps}.items():
        assert s.sha(N / name) == row['after_sha256']
    git('add', '--', *maps, native_support)
    git('diff', '--cached', '--check')
    git('-c', 'user.name=Codex', '-c', 'user.email=codex@localhost', '-c', 'commit.gpgsign=false',
        'commit', '-q', '-m', 'Use explicitly authorized caller-dropping Public namespace fixture')
    source = git('rev-parse', 'HEAD')
    assert not git('status', '--porcelain')
    adoption = copy.deepcopy(parent)
    adoption.update(source_commit=source, source_paths=maps, support_paths=support_maps,
                    previous_adoption={'path': str(args.parent_adoption), 'sha256': s.sha(args.parent_adoption)},
                    adopter_sha256=s.sha(__file__), namespace_admin=authority,
                    correction_capsules=parent.get('correction_capsules', []) + [
                        {'path': str(b.PUBLIC), 'sha256': b.PUBLIC_SHA}, {'path': str(SUPPORT), 'sha256': SUPPORT_SHA}],
                    correction_reviews=parent.get('correction_reviews', []) + [{'path': str(b.PUBLIC_REVIEW), 'sha256': b.PUBLIC_REVIEW_SHA}],
                    failed_prior_attempts_sha256=failures, namespace_history_sha256=namespace_history,
                    runtime_executed=False)
    s.write(out, adoption)
    reference_out.mkdir()
    for name in ('commands.json', 'java-version.log', 'javac-version.log', 'javac.log', 'inputs.json'):
        shutil.copyfile(args.parent_reference.parent / name, reference_out / name)
    derived = copy.deepcopy(reference)
    derived.update(source_commit=source, adoption_sha256=s.sha(out),
                   parent_reference={'path': str(args.parent_reference), 'sha256': s.sha(args.parent_reference)},
                   original_compile_source_commit=reference.get('original_compile_source_commit', reference['source_commit']),
                   original_compile_adoption_sha256=reference.get('original_compile_adoption_sha256', reference['adoption_sha256']),
                   javac_reexecuted=False, reuse_reason='Only namespace helper/test/CI/support glue changed; existing Java helper/JAR/tools/classes byte-identical')
    s.write(reference_out / 'result.json', derived)
    assert b.reference_pins(reference_out / 'result.json') == derived
    print('ADOPTED', source, '31 component paths plus one support path; no javac or subjects')


if __name__ == '__main__':
    main()
