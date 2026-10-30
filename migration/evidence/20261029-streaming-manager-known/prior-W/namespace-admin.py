#!/usr/bin/env python3
"""Fixed reviewed W namespace evidence checks; no subjects or mutation on import."""
import hashlib
import json
import os
import pathlib
import re

R = pathlib.Path('/home/yurii/Java/BiglyBT')
N = R.parent / 'BiglyBTC'
APPROVAL = R / 'build/azureus-public-bound-20261029/admin-fixture/preflight/r3/root-approval.json'
APPROVAL_SHA = '2bb598d383588f26309341cccccfbcbac278b03cb310562c5f098db3c3c484dd'
PREFLIGHT_SHA = '85cccf770fd80ef15d2a01dc5d6687755e8897da7863f80202cb3f0747b910aa'
CAPSULE_SHA = '9c781e5bebea7841cc7d574894367be4460c486f5a9abb99aed7580325f65edb'
REVIEW_SHA = 'e37dae03cd8990634ae7ff25b2ae7577858d6bd216dbc9c90993652eb6c04a18'
HELPER_SHA = '900ffe42f48a5452a78160e254ea8f21575b995ba2a7d3a0c6d066be80826d86'
LAUNCHER = N / 'preview/native/tests/azureus_public_admin_launcher.py'
KEYS = ('BIGLYBTC_PUBLIC_ADMIN_MODE', 'BIGLYBTC_PUBLIC_ADMIN_HELPER',
        'BIGLYBTC_PUBLIC_ADMIN_SHA256', 'BIGLYBTC_PUBLIC_ADMIN_RECEIPT')


def sha(path):
    return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()


def load(path):
    return json.loads(pathlib.Path(path).read_bytes())


def pin(path):
    path = pathlib.Path(path)
    assert path.is_absolute() and path.is_file() and not path.is_symlink()
    return {'path': str(path), 'sha256': sha(path), 'bytes': path.stat().st_size}


def checked_ref(ref):
    assert sha(ref['path']) == ref['sha256']
    return load(ref['path'])


def launch_receipt(path, authority, argv, environment, invoker):
    value = load(path)
    assert value['status'] == 'PASS_AUTHENTICATED_NAMESPACE_SUBJECT' and value['subject_exit'] == 0
    assert value['verified_drop_marker'] is True and value['no_fallback'] is True and value['memfd_seals'] == 15
    assert value['payload_transport'] == 'sealed-fixed-stdin-after-verified-drop'
    assert value['mode'] == authority['mode'] and value['helper'] == {key: authority['helper'][key] for key in ('path', 'sha256', 'bytes')}
    assert value['caller_uid'] == authority['caller_uid'] and value['caller_gid'] == authority['caller_gid']
    assert value['argv'] == [str(pathlib.Path(argv[0]).resolve()), *map(str, argv[1:])]
    assert value['invoker'] == {'path': str(pathlib.Path(invoker).resolve()), 'sha256': authority['invoker']['sha256']}
    assert value['launch_environment'] == environment
    metadata = value['invoker_metadata']
    assert metadata['uid'] == [authority['caller_uid']] * 3 and metadata['gid'] == [authority['caller_gid']] * 3
    command = value['command']
    assert command[0] == authority['authorizer']['path']
    prefix = [command[0], '--disable-internal-agent'] if authority['mode'] == 'pkexec' else [command[0], '-n']
    assert command[:2] == prefix and len(command) == 9 and command[3] == authority['mode']
    assert re.fullmatch(r'/proc/[1-9][0-9]*/fd/[0-9]+', command[2])
    assert command[2] == '/proc/' + command[4] + '/fd/' + command[5]
    assert all(part.isdigit() for part in command[4:8]) and command[5] != command[6]
    assert command[8] == 'fixed-public-fixture-v1'
    assert pin(authority['helper']['path']) == {key: authority['helper'][key] for key in ('path', 'sha256', 'bytes')}
    assert pin(authority['authorizer']['path']) == authority['authorizer']
    return pin(path)


def pins(path):
    """Require actual outer receipt, sole stdout row and root authorization."""
    path = pathlib.Path(path)
    assert path.is_absolute() and sha(path) == PREFLIGHT_SHA and sha(APPROVAL) == APPROVAL_SHA
    approval = load(APPROVAL)
    assert approval['status'] == 'ROOT_REVIEWED_READONLY_ENVIRONMENT_AND_APPROVED_UNCHANGED_PUBLIC_SUBJECTS'
    assert approval['functional_subject_already_executed'] is False and approval['policy_settings_modified'] is False
    assert approval['environment_preflight'] == {'path': str(path), 'sha256': PREFLIGHT_SHA}
    outer = load(path)
    assert outer['status'] == 'PASS_READONLY_NAMESPACE_ENVIRONMENT' and outer['exit'] == 0
    assert outer['source_unchanged'] is True and outer['functional_test_credit'] is False
    assert outer['source_capsule'] == approval['source_capsule'] and outer['source_capsule']['sha256'] == CAPSULE_SHA
    assert outer['source_review'] == approval['source_review'] and outer['source_review']['sha256'] == REVIEW_SHA
    capsule = checked_ref(outer['source_capsule'])
    review = checked_ref(outer['source_review'])
    assert review['capsule'] == outer['source_capsule'] and review['blockers'] == [] and review['execution'] is False
    for name, expected in capsule['artifacts_sha256'].items():
        assert sha(pathlib.Path(outer['source_capsule']['path']).parent / name) == expected
    compile_receipt = checked_ref(outer['compiled_receipt'])
    assert compile_receipt['status'] == 'PASS_COMPILED_FIXED_NAMESPACE_HELPER' and compile_receipt['compiler_exit'] == 0
    assert compile_receipt['source_capsule'] == outer['source_capsule'] and compile_receipt['helper'] == outer['helper'] == approval['helper']
    source = capsule['source_paths']['preview/native/tests/public_namespace_fixture/public_netns_admin.c']
    assert compile_receipt['source'] == {'path': source['after_path'], 'sha256': source['after_sha256']}
    assert sha(source['after_path']) == source['after_sha256'] and outer['helper']['sha256'] == HELPER_SHA
    for ref in (compile_receipt['compiler'], approval['authorizer']):
        assert sha(ref['path']) == ref['sha256']
    compile_root = pathlib.Path(outer['compiled_receipt']['path']).parent
    assert sha(compile_root / 'compile.log') == compile_receipt['compile_log_sha256']
    assert sha(compile_root / 'compile.mk') == compile_receipt['makefile_sha256']
    for name, expected in outer['logs_sha256'].items():
        assert name in ('stdout.log', 'stderr.log') and sha(path.parent / name) == expected
    assert outer['logs_sha256'] == approval['logs_sha256']
    rows = [json.loads(line) for line in (path.parent / 'stdout.log').read_text().splitlines() if line.startswith('{')]
    observation = checked_ref(approval['preflight'])
    assert rows == outer['preflight'] == [observation]
    assert observation['status'] == 'PASS_OWNED_CALLER_NAMESPACE_PREFLIGHT' and observation['functional_test_credit'] is False
    uid, gid = outer['helper']['uid'], outer['helper']['gid']
    assert observation['uid'] == [uid] * 3 and observation['gid'] == [gid] * 3 and observation['groups'] == []
    assert observation['NoNewPrivs'] == 1 and set(observation['capabilities']) == {'CapInh', 'CapPrm', 'CapEff', 'CapBnd', 'CapAmb'}
    assert not any(observation['capabilities'].values()) and 0 < observation['runtime_capabilities'] < 1024
    assert observation['parent_net'] != observation['current_net'] and observation['parent_user'] != observation['current_user']
    assert observation['net_owning_user'] == observation['current_user']
    docs = {(f'203.0.113.{n}', 32) for n in (1, 2, 3)} | {(f'2001:db8::{n}', 128) for n in (1, 2, 3)}
    addresses = [tuple(item) for item in observation['addresses']]
    assert len(addresses) == len(set(addresses)) and docs <= set(addresses) <= docs | {('127.0.0.1', 8), ('::1', 128)}
    for entries in observation['routes'].values():
        for entry in entries:
            assert entry.get('dev', 'lo') == 'lo' and 'gateway' not in entry
            assert entry.get('dst') != 'default' or entry.get('type') in ('unreachable', 'prohibit', 'blackhole')
    launcher = capsule['source_paths']['preview/native/tests/azureus_public_admin_launcher.py']
    authority = {'mode': approval['mode'], 'helper': outer['helper'], 'caller_uid': uid, 'caller_gid': gid,
                 'invoker': {'path': str(LAUNCHER), 'sha256': launcher['after_sha256']},
                 'authorizer': approval['authorizer'], 'preflight': approval['preflight'],
                 'environment_preflight': approval['environment_preflight'], 'source_capsule': approval['source_capsule'],
                 'source_review': approval['source_review'], 'root_approval': pin(APPROVAL),
                 'compiled_receipt': outer['compiled_receipt'], 'readonly_launch': approval['launch']}
    actual_launch = checked_ref(approval['launch'])
    assert actual_launch == outer['launch']
    launch_receipt(approval['launch']['path'], authority, actual_launch['argv'], {}, launcher['after_path'])
    return authority


def environment(authority, receipt):
    receipt = pathlib.Path(receipt)
    assert receipt.is_absolute()
    return dict(zip(KEYS, (authority['mode'], authority['helper']['path'], authority['helper']['sha256'], str(receipt))))
