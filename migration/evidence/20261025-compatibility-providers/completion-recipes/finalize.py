#!/usr/bin/env python3
"""Root-run metadata only, after actual local retention/private qualification."""
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
E_REL = 'migration/evidence/20261025-compatibility-providers'
E = R / E_REL
IMPLEMENTATION = 'c8afff5311e4ff650b17d09651e3db440a0c93e8'
SOURCE = '471ebb75fc66fefb0208b0d086a934b68aced84b'
ORIGINAL = 'cb8c6cb9bb97916cbc7e80338a017200ffc3e71f'
RATING = '1629340340d0bc391d9e582f9ee989a93d550581'
BEFORE_SHA = '9bd4d3547d3b67a7ff87274ae85b4ef0f7e6324f66e0c06ed604946f42826e99'
REVIEW = 'migration/updates/compatibility.providers/20261025.md'
TASK = 'migration/tasks/P08-01-compatibility-providers.md'
COMPONENTS = ('engine.queue_policy', 'library.file_operations', 'ui.classic_file_operations',
              'storage.original_managed_io', 'engine.network_binding', 'plugins.azrating_local_policy',
              'engine.payload_counter_bridge', 'statistics.persistent_peer_accounting')


def require(value, message):
    if not value:
        raise ValueError(message)


def data(path):
    require(path.is_file() and not path.is_symlink() and path.resolve(strict=True) == path,
            'Unsafe/missing input: ' + str(path))
    return path.read_bytes()


def sha(path):
    return hashlib.sha256(data(path)).hexdigest()


def load(path):
    return json.loads(data(path))


def put(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + '\n')


def clean_source():
    require(subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=N, text=True).strip() == SOURCE,
            'Require actual471 metadata parent')
    require(not subprocess.check_output(['git', 'status', '--porcelain', '--untracked-files=all'],
                                       cwd=N, text=True).strip(), 'Native metadata parent is not clean')


def verify(hosted_path):
    require(sha(HERE / 'before.json') == BEFORE_SHA, 'Changed metadata before pins')
    before = load(HERE / 'before.json')
    for rel, digest in before['original_before_sha256'].items():
        require(sha(R / rel) == digest, 'Shared file changed since review: ' + rel)
    for rel in before['new_original_paths']:
        require(not (R / rel).exists(), 'Review/task path already exists: ' + rel)
    require(sha(E / 'verification.json') == before['evidence_verification_sha256'] and
            not (E / 'INCOMPLETE.json').exists(), 'Missing/changed actual retention')
    verification = load(E / 'verification.json')
    require(verification['native_source'] == SOURCE and verification['implementation_source'] == IMPLEMENTATION
            and verification['status'] == 'LOCAL_BOUNDED_CHAPTER_QUALIFIED_SOURCE_PUSHED'
            and verification['full_matrix_rerun'] is False and verification['production_bytes_identical'] is True
            and verification['full_source_snapshots_identical'] is False, 'Wrong qualified source/union')
    require(set(verification['matrix']) == {'gcc', 'clang', 'sanitizers'}, 'Missing compiler lane')
    for lane in verification['matrix'].values():
        require(lane['suites'] == 40 and lane['original_unaffected_credit'] == 38 and
                lane['focused_latest_credit'] == 2 and len(lane['latest_actual_results']) == 40 and
                all(row['passed'] for row in lane['latest_actual_results'].values()) and
                lane['all_production_and_provider_bytes_identical'] is True,
                'Missing actual38+2 qualification')
    actual = verification['actual_original_observations']
    require(all(actual[key] == value for key, value in {
        'priority_frames': 2, 'priority_scalars': 8, 'probe_cases': 7, 'probe_comparisons': 14,
        'nonlinear_corners': 2, 'rate_observations': 23}.items()), 'Wrong original observations')
    require(verification['private_provider']['portable_preflight_cases'] == 7 and
            verification['whole_compatibility_complete'] is False and verification['target_eta_bound'] is False,
            'Scope/preflight gate changed')
    # Existing compact index proves the retained archive/maps. Do not expand or
    # duplicate its1032-member map in shared metadata.
    for rel, digest in load(E / 'artifacts.json')['files_sha256'].items():
        require(sha(E / rel) == digest, 'Changed retained artifact: ' + rel)
    for rel, digest in verification['source_receipts_sha256'].items():
        require(sha(W / rel) == digest, 'Changed actual source receipt: ' + rel)
    push = load(W / 'chapter-current-source-push.json')
    require(push['source_commit'] == push['remote_main'] == SOURCE and push['exit'] == 0,
            'Actual471 remote-main push required')
    private_path = W / 'chapter-private-portable-r3/run-r1/verification/result.json'
    require(sha(private_path) == before['private_result_sha256'], 'Changed private result')
    private = load(private_path)
    require(private['status'] == 'PRIVATE_B0E_RUNTIME_QUALIFIED_NOT_PROMOTED' and
            private['source'] == SOURCE and private['implementation_source'] == IMPLEMENTATION and
            private['smoke_qt_rows'] == 13 and len(private['captures']) == 28 and
            private['packaged_aliases'] == 6 and private['all_elf_rpaths'] == 50 and
            private['distribution_scope'] == 'private-qualification-only' and
            private['promoted'] is False and private['shipping_approved'] is False and
            private['causal_qualified'] is False, 'Missing actual private qualification')
    positives = {'provider-payload-counter', 'selected-swarm-admission', 'persistent-native-peer-session',
                 'managed-download', 'managed-combined-protocol'}
    require({row['name'] for row in private['cohort']} == positives | {'provider-next-runtime-deployment'}
            and all(row['status'] == 'PASS' for row in private['cohort']) and
            next(row for row in private['cohort'] if row['name'] == 'provider-next-runtime-deployment')['cases'] == 11,
            'Five positives plus deployment11 required')
    for name, digest in private['captures'].items():
        require(sha(private_path.parent / 'captures' / name) == digest, 'Changed actual capture')
    require(sha(N / 'dist/BUILD_MANIFEST.json') == private['previous_dist_manifest_sha256'] ==
            'deefcbe40681c1cd2d7352869be399d4e75d5d935a691bdc921fba11db71b221',
            'Previously published dist changed; this metadata does not promote')
    require(sha(N / 'README.md') == before['native_before_sha256']['README.md'], 'Native README changed')
    clean_source()
    hosted = None
    if hosted_path:
        p = Path(hosted_path)
        require(not p.is_absolute() and '..' not in p.parts, 'Use relative W hosted receipt')
        hosted = load(W / p)
        require(hosted['source_commit'] == SOURCE and all(run['headSha'] == SOURCE for run in hosted['runs']),
                'Hosted snapshot is for another source')
    return before, verification, private, private_path, hosted


def prepend(text, current):
    pos = text.index('\n') + 1
    return text[:pos] + '\n' + current + '\n\n## Historical records\n\n' + text[pos:]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--hosted-receipt', help='Optional actual W-relative status snapshot; no success inferred')
    parser.add_argument('--write', action='store_true')
    args = parser.parse_args()
    before, verification, private, private_path, hosted = verify(args.hosted_receipt)
    link = E_REL + '/completion.json'
    summary = ('Current bounded compatibility-provider milestone is qualified: implementation `' + IMPLEMENTATION +
        '`, test-only qualification followup `' + SOURCE + '`. Effective GCC/Clang/leak-enabled ASan-UBSan lanes each '
        'credit38 original unaffected PASS +2 focused followup PASS =40; no full40 rerun or identical full-snapshot claim. '
        'Actual original observations: priority8, probes14, nonlinear corners2, rate arithmetic23; packaging preflight7. '
        'Private b0e/3342 candidate passes13 smoke Qt rows,28 captures,5 positive cohorts and11 deployment controls. '
        'It remains private-only and unpromoted; published dist deefcbe remains unchanged. '
        'No full CF closure, new causal/DHT/public-wire, bound target ETA, native SOCKS4a/USERID admission or imported '
        'Scheduler recovery is claimed. All CF01–CF08 remain open.')
    boundary = ('Latest explicit user boundary: finish this current milestone, then exactly two further milestones '
        'in the same compatibility chapter—verified native SOCKS4a/USERID admission and imported-original Scheduler '
        'pause recovery—then STOP. Both are pending. No next chapter is authorized. This boundary supersedes older '
        'open-ended continuation text preserved below as history.')
    review = ('# Compatibility-provider bounded port review\n\n' + summary + '\n\n' + boundary +
        '\n\nOriginal BiglyBT baseline/reviewed source: `' + ORIGINAL + '`. Azrating original baseline: `' + RATING +
        '`. Upstream repositories and all broad first_ported/integrated-through values stay unchanged. This is '
        'additional native implementation at the existing baselines. See [retained review](../../evidence/'
        '20261025-compatibility-providers/REVIEW.md) and [completion](../../evidence/'
        '20261025-compatibility-providers/completion.json).\n\nOrdinary queue priority/probe/global-rate facts, '
        'sequential independent bulk operations, local-rating held-lease CAS editing, nonlinear-tail raw IO corners, '
        'wildcard binding and optional ABI2 payload/protocol observations are qualified only within those records. '
        'Rate traces cover selected methods/containers, not whole timer/peer/provider parity; corners cover raw '
        'FMFile bytes, not GlobalManager progress. The two failed SAN wrappers and all interrupted/failed attempts '
        'remain retained. Original GCC/Clang40 PASS plus SAN38 PASS are preserved; only the two affected cases '
        'were repeated after five test-only fortified-read fixes.\n\nThe private dependency is b0e55bfbdc2be5dc2ebbefc953837d64c9e5151b0305332335071e37b6956937 '
        '(9085072 bytes), manifest3342ec8ae72ecafa4a1b887e664d0d066fffce4f7d864b0b130ca903be953a3a, '
        'record3fcd415424d71e6082022f8be6bb1a75036a0fbf3b228228813e1a4cee1ffdca. '
        'Its1074/451 source leaves,285 installed leaves and12 exports are source-bound private qualification; '
        'the old selected/causal dependency records and notices remain intact. Direct provider SOCKS tests do not '
        'establish native SOCKS4a admission. Test-only QtTest may use the retained installation; shipping Qt/ICU '
        'and all six LT aliases are checked in the private bundle.\n\nRoot performed actual execution/integration '
        'and publication. Parallel workers supplied source capsules and scoped reviews; no independent full-product '
        'certification is claimed. Hosted status is an optional separately retained snapshot and contributes no '
        'private runtime or additional local matrix credit.\n')
    docs = {}
    for rel in ('migration/STATE.md', 'MIGRATION_PLAN.md'):
        ref = 'evidence/20261025-compatibility-providers/completion.json' if rel.startswith('migration/') else link
        docs[rel] = prepend((R / rel).read_text(), summary + '\n\n' + boundary + '\n\n[Completion](' + ref + ').')
    rel = 'migration/tasks/COMPATIBILITY-FOLLOWUPS.md'
    text = (R / rel).read_text()
    replacements = {
        'Unsupported EOF-short/other corners keep this item open.':
            'The current bounded source qualifies the tested short-tail reordered/reordered-compact corners; other layouts/corners keep this item open.',
        'Still implement/qualify lightweight/reserved/flexible slots, active speed/ETA\n  probes, external/tag-specific FP/limits, disk-latency/smoothed-rate/stale-rank/\n  file-priority observations and new-seed/group saved-position normalization.':
            'Ordinary file-priority facts, active speed-probe states and a global rate hook now qualify in the current bounded batch. Still qualify external/tag/lightweight providers, independently sourced target smoothed ETA, disk-latency/stale-rank inputs and new-seed/group saved-position normalization.',
        'Still implement bulk operations, archive-deletion controls, advanced/virtual/':
            'Sequential reviewed per-item bulk operations now qualify in the current bounded batch. Still implement archive-deletion controls, advanced/virtual/'
    }
    for old, new in replacements.items():
        require(text.count(old) == 1, 'Changed checklist anchor: ' + old)
        text = text.replace(old, new)
    docs[rel] = prepend(text, summary + '\n\n' + boundary + '\n\n[Current completion](../evidence/20261025-compatibility-providers/completion.json).')
    require(docs[rel].count('- [ ] **CF-') == 8, 'All eight CF parents must remain open')
    task_note = '# P08-01 — compatibility-provider followup\n\n' + summary + '\n\n' + boundary + '\n\n'
    task_note += ('- [x] Current bounded implementation/source push, exact38+2 local lanes, finite original observations and private candidate qualification.\n'
                  '- [ ] Verified native SOCKS4a/USERID admission.\n- [ ] Imported-original Scheduler pause recovery.\n'
                  '- [ ] Broader CF01–CF08 compatibility (outside the current two-milestone authorization).\n\n'
                  '[Completion](../evidence/20261025-compatibility-providers/completion.json) and '
                  '[review](../updates/compatibility.providers/20261025.md). No system installation or public-provider claim.\n')
    docs[TASK], docs[REVIEW] = task_note, review
    rel = 'migration/tasks/P08-01-queue-seeding-scheduling.md'
    docs[rel] = (R / rel).read_text() + '\n## Bounded compatibility-provider followup (2026-10-25)\n\n' + summary + '\n\n' + boundary + '\n\n[Task](P08-01-compatibility-providers.md).\n'
    registry = load(R / 'migration/upstreams.lock.json')
    old_registry = json.loads(json.dumps(registry))
    components = {row['id']: row for row in registry['components']}
    for cid in COMPONENTS:
        row = components[cid]
        for commit, purpose in ((IMPLEMENTATION, 'implementation'), (SOURCE, 'tests')):
            if not any(item['commit'] == commit for item in row['native_commits']):
                row['native_commits'].append({'repository': 'https://github.com/d00rsfan/BiglyBTC.git', 'commit': commit, 'purpose': purpose})
        for key, value in (('review_records', REVIEW), ('evidence', link), ('task_records', TASK)):
            if value not in row[key]:
                row[key].append(value)
        prior = next(item for item in old_registry['components'] if item['id'] == cid)
        require(row['state'] == prior['state'] and row['source_coverage'] == prior['source_coverage'] and
                row['target_paths'] == prior['target_paths'], 'Broad source/target scope changed')
    patch = W / 'provider-next/changes.patch'
    require(sha(patch) == before['provider_patch_sha256'], 'Frozen provider patch changed')
    patch_rel = E_REL + '/provider-patches/provider-next.patch'
    notices = ['migration/notices/libtorrent-2.0.12-LICENSE', 'migration/notices/libtorrent-2.0.12-COPYING',
               'migration/notices/libtorrent-2.0.12-debian-copyright']
    require(all((R / rel).is_file() for rel in notices), 'Missing original dependency notices')
    dependency = {'id': 'bounded-engine-libtorrent-private-traffic-v2', 'name': 'Private LT traffic ABI2 provider (not shipping approval)',
        'version': '2.0.12', 'repository_url': 'https://github.com/arvidn/libtorrent.git',
        'source_commit': '740a0b9aeabe00e762cc0efe4a0f27593db2550b',
        'artifacts': [{'url': 'file://' + str(W / 'provider-next/install/lib/libtorrent-rasterbar.so.2.0.12'),
                       'sha256': private['provider_sha256']}],
        'build_recipe': 'Private source-bound qualification only, not shipping/causal/full-client approval. '
            'Exact b0e artifact9085072 bytes; manifest3342ec8ae72ecafa4a1b887e664d0d066fffce4f7d864b0b130ca903be953a3a; '
            'build record3fcd415424d71e6082022f8be6bb1a75036a0fbf3b228228813e1a4cee1ffdca. '
            '1074 full/451 selected source leaves,285 installed leaves,12 exports. Actual r1build/install/provider tests, '
            'r2Boost fix and r3SOCKS fixtures are retained verbatim in the compressed evidence bundle. See ' + E_REL +
            '/completion.json and retained-inputs.tar.gz:MEMBERS.json, provider-next/continuations/r3/execution/'
            'candidate-source-build-record.json. Existing e74/d367/c35/causal records remain unchanged. Native4a admission '
            'and Scheduler imported recovery remain pending; there is no inherited causal proof.',
        'patches': [{'path': patch_rel, 'sha256': sha(patch)}], 'notices': notices}
    require(not any(row['id'] == dependency['id'] for row in registry['external_dependencies']), 'Dependency already recorded')
    registry['external_dependencies'].append(dependency)
    require(registry['external_dependencies'][:-1] == old_registry['external_dependencies'], 'Old dependencies changed')
    docs['migration/upstreams.lock.json'] = json.dumps(registry, indent=2, ensure_ascii=False) + '\n'
    readme = (N / 'README.md').read_text() + '\n\nBounded compatibility-provider followup (2026-10-25): implementation `' + IMPLEMENTATION + '`, test-only qualified source `' + SOURCE + '`. Ordinary queue/file-priority/probe/rate facts, sequential bulk file operations, local rating editing, nonlinear-tail corners and optional combined counters are qualified in [the current record](' + link + '). Effective40 local suites per compiler are a38+2 results union. The b0e runtime candidate remains private-only/unpromoted; no new causal/full-CF claim. Verified native SOCKS4a/USERID and imported Scheduler recovery are the two pending milestones in this same chapter, then STOP.\n'
    require(readme.startswith((N / 'README.md').read_text()), 'Historical README was rewritten')
    if not args.write:
        print(json.dumps({'status': 'TRACKING_READY_AFTER_ACTUAL_RETENTION_AND_PRIVATE_PASS',
                          'original_docs': sorted(docs), 'prepared_native_readme': True,
                          'effective_lanes': '38+2=40 per lane', 'hosted_snapshot': hosted}))
        return
    require(not (E / 'completion.json').exists(), 'Completion already finalized')
    for rel, text in docs.items():
        p = R / rel; p.parent.mkdir(parents=True, exist_ok=True); p.write_text(text)
    completion = {'status': 'BOUNDED_CURRENT_CHAPTER_QUALIFIED_PRIVATE_NOT_PROMOTED',
        'implementation_source': IMPLEMENTATION, 'qualified_source': SOURCE,
        'effective_suites_per_lane': 40, 'credit': '38 unaffected original +2 focused471; no whole40 rerun',
        'private_smoke_qt_rows': private['smoke_qt_rows'], 'private_capture_count': len(private['captures']),
        'private_positive_cohorts': 5, 'private_deployment_cases': 11,
        'private_manifest_sha256': private['manifest_sha256'], 'private_result_sha256': sha(private_path),
        'retention_verification_sha256': sha(E / 'verification.json'), 'hosted_status_snapshot': hosted,
        'hosted_private_runtime_or_extra_local_credit': False, 'all_CF01_CF08_open': True,
        'next_two_pending': ['Verified native SOCKS4a/USERID admission', 'Imported-original Scheduler pause recovery'],
        'stop_after_next_two': True, 'new_causal_or_target_ETA_or_public_DHT_or_shipping_approval': False}
    put(E / 'completion.json', completion)
    dest = E / 'provider-patches/provider-next.patch'; dest.parent.mkdir(exist_ok=True); shutil.copy2(patch, dest)
    put(E / 'private-portable-result.json', private)
    recipes = E / 'completion-recipes'; recipes.mkdir()
    for p in HERE.iterdir():
        if p.is_file():
            shutil.copy2(p, recipes / p.name)
    put(recipes / 'files.json', {p.name: sha(p) for p in recipes.iterdir() if p.is_file()})
    output = HERE / 'output'; output.mkdir(); (output / 'README.md').write_text(readme)
    exports = {rel: {'source': str(R / rel), 'sha256': sha(R / rel),
                     'before_sha256': before['native_before_sha256'][rel]} for rel in docs}
    exports['README.md'] = {'source': str(output / 'README.md'), 'sha256': sha(output / 'README.md'),
                           'before_sha256': before['native_before_sha256']['README.md']}
    for p in E.rglob('*'):
        if p.is_file():
            exports[str(p.relative_to(R))] = {'source': str(p), 'sha256': sha(p), 'before_sha256': None}
    put(W / 'current-metadata-plan.json', {'source': SOURCE, 'product_source': SOURCE,
        'implementation_source': IMPLEMENTATION, 'parent': SOURCE, 'evidence': E_REL,
        'tracking_files': sorted(docs), 'exports': exports})
    print(json.dumps({'status': completion['status'], 'docs_written': len(docs),
                      'export_plan': str(W / 'current-metadata-plan.json'), 'native_not_modified': True}))


if __name__ == '__main__':
    main()
