#!/usr/bin/env python3
"""Exact optional same-image Public/private session cohort; no target execution."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET

MANIFEST_SHA = '62f7545f96af46806e7a7708213319ffd1e91c7a5228f0c35065eaa249a91343'
RECORD_SHA = '449aa82f665786ce63f4e1778cee23bf07ca45439b762cf283b67efd0d48af0f'
PRIVATE_APPROVAL_SHA = 'f3ca7de94ca6beb12e0375293c9c74c3f78a34f7e4952e1a122ff0ae9dff1f83'
SOURCE_CAPSULE = '63e9925c8f4f3e80168425d22b62ef87b23732eccff8fa1b7714f73f3f84e9c3'
DSO_SHA = '7ffbd4c63d7770168bcbdf8e8c19644e18d1509d57ac148331a3c4bd094310c4'
SDK_SHA = '869833b2302494bb95d910f74261e60e2e96446bcc85c3f1e2412b8448a07e3b'
COMMON_SHA = 'db8dc81c8d2baf5cf3859d5706bacbea68253866bdbfc03b19d24cdcf2a7cb6c'
CASES = ['same-image-34-exports-and-markers', 'distinct-roles-public-and-private-admission-denials',
         'loopback-tracker-peer-transfer-survives-private-withdraw',
         'private-mixed-settings-failure-is-session-local',
         'public-join-preserves-private-registration-and-independent-join',
         'joined-public-disk-bytes-exact']


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def common():
    p = Path(__file__).with_name('verify_runtime_manifest.py')
    assert not p.is_symlink() and digest(p) == COMMON_SHA, 'Changed existing map verifier'
    sys.dont_write_bytecode = True
    spec = importlib.util.spec_from_file_location('biglybtc_coexistence_map_reader', p)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def inspect(args):
    c = common()
    manifest_path = c.regular(args.manifest)
    assert digest(manifest_path) == MANIFEST_SHA, 'Unreviewed coexistence cohort'
    m = json.loads(manifest_path.read_bytes())
    assert m['schema'] == 1 and m['purpose'] == 'same-image-separate-public-private-sessions'
    assert (m['source_build_record_sha256'], m['source_capsule_sha256'], m['artifact_sha256'],
            m['artifact_bytes'], m['sdk_tree_sha256']) == (RECORD_SHA, SOURCE_CAPSULE, DSO_SHA, 9659328, SDK_SHA)
    approval_path = c.regular(args.approval)
    assert re.fullmatch('[0-9a-f]{64}', args.approval_sha256) and digest(approval_path) == args.approval_sha256
    approval = json.loads(approval_path.read_bytes())
    assert approval['schema'] == 'BIGLYBTC_PUBLIC_PRIVATE_COEXISTENCE_APPROVAL_V1'
    assert approval['status'] == 'ROOT_APPROVED_PUBLIC_PRIVATE_COEXISTENCE'
    assert approval['scope'] == m['purpose'] and approval['runtime_manifest_sha256'] == MANIFEST_SHA
    assert approval['source_build_record_sha256'] == RECORD_SHA and approval['artifact_sha256'] == DSO_SHA
    assert approval['private_approval_sha256'] == PRIVATE_APPROVAL_SHA
    assert re.fullmatch('[0-9a-f]{64}', approval['review_sha256'])
    assert digest(c.regular(args.private_approval)) == PRIVATE_APPROVAL_SHA
    private = json.loads(args.private_approval.read_bytes())
    assert (private['schema'], private['source_capsule_sha256'], private['artifact_sha256'],
            private['sdk_tree_sha256'], private['source_build_record_sha256']) == (
            'BIGLYBTC_PRIVATE_I2P_OWNER_APPROVAL_V1', SOURCE_CAPSULE, DSO_SHA, SDK_SHA, RECORD_SHA)
    record_path = c.regular(Path(m['source_build_record']))
    assert digest(record_path) == RECORD_SHA
    r = json.loads(record_path.read_bytes())
    assert r['schema'] == 'BIGLYBTC_PRIVATE_I2P_MULTI_SOURCE_RECORD_V1'
    assert r['status'] == 'BUILT_DIRECTLY_TESTED_NOT_APPROVED'
    assert (r['source_capsule_sha256'], r['artifact_sha256'], r['artifact_bytes'], r['sdk_tree_sha256']) == (
        SOURCE_CAPSULE, DSO_SHA, 9659328, SDK_SHA)
    source, install = [c.canonical_directory(r[k]) for k in ('source_root', 'install_root')]
    c.verify_map(r, 'source_inputs_sha256', source, 1087)
    c.verify_map(r, 'installed_sha256', install, 293)
    assert {str(p.relative_to(source)) for p in source.rglob('*') if p.is_file()} == set(r['source_inputs_sha256'])
    assert {str(p.relative_to(install)) for p in install.rglob('*') if p.is_file()} == set(r['installed_sha256'])
    headers = {k.removeprefix('include/'): v for k, v in r['installed_sha256'].items() if k.startswith('include/')}
    assert len(headers) == 284 and headers == r['headers_sha256'] == private['headers_sha256']
    assert all(r['source_inputs_sha256']['include/' + k] == v for k, v in headers.items())
    assert hashlib.sha256(json.dumps(headers, sort_keys=True, separators=(',', ':')).encode()).hexdigest() == SDK_SHA
    artifact = c.regular(Path(r['artifact']))
    assert artifact == install / 'lib/libtorrent-rasterbar.so.2.0.12'
    assert args.library.resolve(strict=True) == artifact and args.include_root.resolve(strict=True) == install / 'include'
    assert digest(artifact) == DSO_SHA and artifact.stat().st_size == 9659328
    for name, expected in r['evidence_sha256'].items():
        assert digest(c.regular(record_path.parent / name)) == expected, name
    runtime = json.loads(c.regular(Path(m['private_runtime_manifest'])).read_bytes())
    assert digest(Path(m['private_runtime_manifest'])) == private['runtime_manifest_sha256']
    assert len(r['required_exports']) == 34 and set(r['required_exports']) == set(runtime['required_exports'])
    assert r['actual_controls']['actual_ctest_suites'] == 9 and r['actual_controls']['private_owner_methods'] == 10
    proof = m['role_controls']
    for path, expected in proof['files_sha256'].items():
        assert digest(c.regular(Path(path))) == expected, path
    result = json.loads(Path(proof['result']).read_bytes())
    assert result['status'] == 'PASS_SDK_PUBLIC_PRIVATE_ROLES_NOT_APPROVED'
    assert result['record_sha256'] == RECORD_SHA and result['artifact_sha256'] == DSO_SHA
    assert result['source_and_sdk_unchanged'] is True and result['native_runtime_approved'] is False
    assert result['actual_cases'] == CASES and digest(Path(result['image'])) == result['image_sha256']
    before, after = [json.loads(Path(proof[k]).read_bytes()) for k in ('inputs_before', 'inputs_after')]
    assert before == after and before['source_sha256'] == r['source_inputs_sha256']
    assert before['installed_sha256'] == r['installed_sha256']
    commands = json.loads(Path(proof['commands']).read_bytes())
    assert [x['name'] for x in commands] == ['configure', 'build', 'roles']
    assert all(type(x['exit']) is int and x['exit'] == 0 for x in commands)
    root = ET.parse(proof['xml']).getroot()
    assert all(root.get(k) == v for k, v in [('tests', '6'), ('failures', '0'), ('errors', '0'), ('skipped', '0')])
    cases = list(root.iter('testcase'))
    assert [x.get('name') for x in cases] == CASES and all(x.get('status') == 'run' and not list(x) for x in cases)
    return m


def main():
    p = argparse.ArgumentParser()
    for name in ('manifest', 'library', 'approval', 'private-approval', 'header', 'receipt'):
        p.add_argument('--' + name, required=True, type=Path)
    p.add_argument('--include', dest='include_root', required=True, type=Path)
    p.add_argument('--approval-sha256', required=True)
    args = p.parse_args()
    m = inspect(args)
    text = '#pragma once\n#define BIGLYBTC_PERSISTENT_RUNTIME_BOUND 1\n#define BIGLYBTC_PERSISTENT_COEXISTENCE_BOUND 1\n'
    text += 'namespace biglybtc::engine::detail::runtime_binding {\n'
    for name, value in [('artifact', m['artifact']), ('sha256', DSO_SHA), ('manifest', MANIFEST_SHA),
                        ('coexistence_approval_sha256', args.approval_sha256)]:
        text += 'inline constexpr char ' + name + '[] = ' + json.dumps(value) + ';\n'
    args.header.write_text(text + 'inline constexpr unsigned long long bytes = 9659328ULL;\n}\n')
    args.receipt.write_text(json.dumps({'manifest_sha256': MANIFEST_SHA, 'artifact_sha256': DSO_SHA,
        'approval_sha256': args.approval_sha256, 'private_approval_sha256': PRIVATE_APPROVAL_SHA,
        'same_image_exports': 34, 'scope': m['purpose'], 'fresh_sdk_role_cases': 6,
        'native_coexistence_qualification_pending': True}, indent=2) + '\n')


if __name__ == '__main__':
    main()
