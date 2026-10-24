#!/usr/bin/env python3
"""Two owned Java -> managed native partial/complete -> actual Java reload cases."""
import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
ORIGINAL = 'cb8c6cb9bb97916cbc7e80338a017200ffc3e71f'
JAVA = Path('/usr/lib/jvm/java-17-openjdk-amd64/bin')

def require(value, reason):
    if not value:
        raise ValueError(reason)

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def regular(path):
    info = path.lstat()
    require(stat.S_ISREG(info.st_mode) and info.st_nlink == 1 and info.st_uid == os.geteuid(),
            'Non-owned/nonregular input: ' + str(path))
    return info

def source_map(root):
    result = {}
    for path in sorted(root.rglob('*')):
        require(not path.is_symlink(), 'Symlink in retained class cohort')
        if path.is_file():
            regular(path)
            result[path.relative_to(root).as_posix()] = sha(path)
    return result

def physical(path):
    info = regular(path)
    return (info.st_dev, info.st_ino, info.st_size, sha(path))

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--driver', type=Path, required=True)
    parser.add_argument('--reference-root', type=Path, required=True)
    parser.add_argument('--classes', type=Path, default=Path('/tmp/biglybtc-oracle/classes'))
    parser.add_argument('--retention', type=Path, default=Path('/home/yurii/Java/BiglyBTC/build/dependencies/java-oracle-17-20261021/retention.json'))
    parser.add_argument('--work', type=Path, required=True)
    args = parser.parse_args()
    manifest = json.loads((HERE / 'manifest.json').read_text())
    for name, digest in manifest['capsule_sha256'].items():
        require(sha(HERE / name) == digest, 'Changed handoff source: ' + name)
    reference = args.reference_root.resolve(strict=True)
    for name, digest in manifest['reference_sha256'].items():
        require(sha(reference / name) == digest, 'Changed pinned helper: ' + name)
    regular(args.retention)
    require(sha(args.retention) == manifest['java_retention_sha256'], 'Changed retained Java identity')
    retained = json.loads(args.retention.read_text())
    require(retained['source_commit'] == ORIGINAL and retained['recompiled'] is False, 'Wrong original Java cohort')
    classes = args.classes.resolve(strict=True)
    require(classes == args.classes and classes.is_relative_to('/tmp'), 'ReferenceFence requires canonical generated /tmp classes')
    require(source_map(classes) == retained['file_sha256'], 'Original compiled core/resources changed')
    driver = args.driver.resolve(strict=True)
    regular(driver)
    work = args.work.absolute()
    require(work.parent == Path('/tmp') and work.name.startswith('biglybtc-managed-handoff-')
            and work.resolve() == work and not work.exists() and not work.is_symlink(), 'Fresh owned /tmp handoff root required')
    work.mkdir(mode=0o700)
    (work / 'classes').mkdir(mode=0o700)
    (work / 'empty-sourcepath').mkdir(mode=0o700)
    # Existing helpers are reused verbatim. Compile only six small fixture
    # classes in this one batch, never any original implementation/project.
    helper_names = ['ReferenceFence.java', 'EngineOracle.java', 'InteropOracle.java', 'MultiFileOracle.java', 'MultiEngineOracle.java']
    sources = [reference / 'migration/fixtures/java' / name for name in helper_names] + [HERE / 'ManagedHandoffOracle.java']
    env = {key: value for key, value in os.environ.items()
           if key not in ('CLASSPATH', 'JAVA_TOOL_OPTIONS', '_JAVA_OPTIONS', 'JDK_JAVA_OPTIONS', 'LD_PRELOAD')}
    env['PYTHONDONTWRITEBYTECODE'] = '1'
    commands = []
    def run(name, argv, cwd, timeout):
        row = {'name': name, 'argv': list(map(str, argv)), 'cwd': str(cwd), 'timeout_seconds': timeout, 'state': 'started'}
        commands.append(row)
        (work / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
        with (work / (name + '.stdout')).open('xb') as out, (work / (name + '.stderr')).open('xb') as err:
            result = subprocess.run(list(map(str, argv)), cwd=cwd, env=env, stdout=out, stderr=err, timeout=timeout)
        row.update(state='finished', exit_code=result.returncode)
        (work / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
        require(result.returncode == 0, name + ' failed; actual raw logs retained')
        return (work / (name + '.stdout')).read_text(errors='replace')
    run('javac-fixtures', [JAVA / 'javac', '-J-XX:ActiveProcessorCount=2', '--release', '8', '-encoding', 'UTF-8',
        '-proc:none', '-implicit:none', '-sourcepath', work / 'empty-sourcepath', '-cp', classes,
        '-d', work / 'classes', *sources], work, 90)
    sys.path.insert(0, str(reference / 'migration/tools'))
    from peer_interop import jvm, unused_port
    from handoff import decode
    classpath = str(work / 'classes') + ':' + str(classes)
    def decoded(path):
        regular(path)
        raw = path.read_bytes()
        require(len(raw) <= 4 * 1024 * 1024, 'Unexpected generated state size')
        if raw.startswith(b'\x1f\x8b'):
            raw = gzip.decompress(raw)
            require(len(raw) <= 4 * 1024 * 1024, 'Unexpected generated decoded size')
        return decode(raw)
    def observe(label, java, expected, pieces, mode):
        port = unused_port()
        text = run(label, [*jvm(java, classpath, (port,)), 'ManagedHandoffOracle', port, pieces, expected, mode], java, 150)
        rows = [line.split('\t') for line in text.splitlines() if line.startswith('HANDOFF_')]
        result = {'files': {}}
        for row in rows:
            if row[0] == 'HANDOFF_FILE':
                require(len(row) == 7 and row[3] in ('true', 'false'), 'Malformed actual file observation')
                index = int(row[1]); require(index not in result['files'], 'Duplicate actual file')
                result['files'][index] = {'priority': int(row[2]), 'skipped': row[3] == 'true',
                    'downloaded': int(row[4]), 'bytes': int(row[5]), 'path': Path(bytes.fromhex(row[6]).decode('utf-8'))}
            elif row[0] == 'HANDOFF_INTENT':
                require(len(row) == 7 and 'intent' not in result and row[2] in ('true', 'false')
                        and row[3] in ('true', 'false'), 'Malformed actual intent observation')
                result['intent'] = row[2:]  # Runtime state is diagnostic, not saved-state equality.
            elif row[0] == 'HANDOFF_HASH':
                require(len(row) == 2 and 'hash' not in result, 'Malformed actual hash observation'); result['hash'] = row[1]
            elif row[0] == 'HANDOFF_COUNTERS':
                require(len(row) == 3 and 'counters' not in result, 'Malformed actual counters'); result['counters'] = tuple(map(int, row[1:]))
            elif row[0] == 'HANDOFF_READY':
                require(len(row) == 3 and 'pieces' not in result, 'Malformed actual checked-piece observation'); result['pieces'] = tuple(map(int, row[1:]))
            elif row[0] == 'HANDOFF_ORIGINAL_STOPPED':
                require(len(row) == 1 and 'stopped' not in result, 'Duplicate actual stop'); result['stopped'] = True
            else:
                raise ValueError('Unexpected handoff marker')
        require(set(result) == {'files', 'intent', 'hash', 'counters', 'pieces', 'stopped'} and result['files']
                and result['pieces'] == (pieces, 8), 'Incomplete actual original reload')
        require(set(result['files']) == set(range(len(result['files']))), 'Unexpected original file indices')
        for item in result['files'].values():
            require(item['path'].resolve() == item['path'] and item['path'].is_relative_to(java / 'payload'), 'Unexpected actual payload mapping')
        return result
    def policies(actual):
        return {index: (item['priority'], item['skipped'], item['bytes']) for index, item in actual['files'].items()}
    def preserve(before, after, old_spans, new_spans):
        require(len(before[b'downloads']) == len(after[b'downloads']) == 1, 'Unexpected saved library cardinality')
        for key, value in before.items():
            if key != b'downloads': require(after.get(key) == value, 'Unknown inventory root value changed')
        require(set(after) == set(before), 'Native added unexpected inventory root fields')
        a, b = before[b'downloads'][0], after[b'downloads'][0]
        require(set(b) <= set(a) | {b'downloaded', b'uploaded', b'completedbytes', b'allocated'}, 'Native added unexpected entry fields')
        for key, value in a.items():
            if key not in (b'downloaded', b'uploaded', b'completedbytes', b'allocated'):
                require(b.get(key) == value, 'Saved intent/policy changed: ' + repr(key))
        for key in (b'info', b'unknown.future.plugin'):
            require(old_spans[(key,)] == new_spans[(key,)], 'Original metadata/unknown raw subtree changed')
    summaries = {}
    for kind in ('single', 'multifile'):
        case = work / kind; case.mkdir(mode=0o700)
        generation = case / 'generation'; generation.mkdir(mode=0o700)
        fixture = generation / 'fixtures'
        if kind == 'single':
            run(kind + '-generate', [*jvm(generation, classpath), 'InteropOracle', 'generate', fixture], generation, 90)
            metadata = fixture / 'profile/generated-1.torrent'; expected = fixture / 'generated.bin'
        else:
            run(kind + '-generate', [*jvm(generation, classpath), 'MultiFileOracle', fixture], generation, 90)
            metadata = fixture / 'multi-1.torrent'; expected = fixture / 'expected/multi-fixture'
        java = case / 'java'; java.mkdir(mode=0o700)
        shutil.copyfile(metadata, java / 'input.torrent')
        port = unused_port()
        command = ([*jvm(java, classpath, (port,)), 'EngineOracle', port, 0, java / 'input.torrent', expected, 'prepare-partial']
            if kind == 'single' else [*jvm(java, classpath, (port,)), 'MultiEngineOracle', port, 0,
                java / 'input.torrent', expected, 'prepare', 'priorities'])
        run(kind + '-prepare', command, java, 180)
        profile = java / 'profile'
        baseline = observe(kind + '-baseline', java, expected, 1, 'partial')
        identities = {i: physical(f['path'])[:2] for i, f in baseline['files'].items()}
        initial_paths = {i: f['path'] for i, f in baseline['files'].items()}
        require(any(path.name.endswith('.az!') for path in initial_paths.values()), 'Original incomplete mapping was not created')
        for mode in ('partial', 'complete'):
            config_before = (profile / 'biglybt.config').read_bytes()
            before, _ = decoded(profile / 'downloads.config')
            old_state, old_spans = decoded(profile / 'active' / (baseline['hash'] + '.dat'))
            output = case / ('native-' + mode)
            run(kind + '-native-' + mode, [driver, work, profile, java / 'input.torrent', expected, output, mode], case, 120)
            receipt = json.loads((output / 'result.json').read_text())
            require(receipt['mode'] == mode and receipt['hash'] == baseline['hash'] and receipt['session_joined'] is True
                    and receipt['storage_released'] is True and int(receipt['received']) > 0
                    and receipt['pieces'] == (7 if mode == 'partial' else 8), 'Missing actual managed progress/join proof')
            require((profile / 'biglybt.config').read_bytes() == config_before, 'Native changed original global settings')
            after, _ = decoded(profile / 'downloads.config')
            native_state, new_spans = decoded(profile / 'active' / (baseline['hash'] + '.dat'))
            preserve(before, after, old_spans, new_spans)
            old_entry, new_entry = before[b'downloads'][0], after[b'downloads'][0]
            require(new_entry[b'downloaded'] == old_entry.get(b'downloaded', 0) + int(receipt['received'])
                    and new_entry[b'uploaded'] == old_entry.get(b'uploaded', 0) + int(receipt['sent']), 'Native admission counters were charged incorrectly')
            require(new_entry[b'completedbytes'] == receipt['pieces'] * 16384, 'Native verified progress does not match actual pieces')
            native_files = native_state[b'attributes'][b'filedownloaded'][b'downloaded']
            actual = observe(kind + '-java-' + mode, java, expected, receipt['pieces'], mode)
            require(actual['hash'] == baseline['hash'] and actual['intent'] == baseline['intent']
                    and policies(actual) == policies(baseline), 'Original reload interpreted different intent/priorities/limits')
            require(actual['counters'] == (new_entry[b'downloaded'], new_entry[b'uploaded']), 'Original manager did not restore native saved counters')
            require(len(native_files) == len(actual['files']), 'Original file cardinality changed')
            for index, item in actual['files'].items():
                require(item['downloaded'] == native_files[index], 'Original per-file checked progress differs')
                require(physical(item['path'])[:2] == identities[index], 'Managed/Java handoff replaced an original payload inode')
                wanted = initial_paths[index]
                if item['downloaded'] == item['bytes'] and wanted.name.endswith('.az!'):
                    wanted = wanted.with_name(wanted.name[:-4])
                    require(not initial_paths[index].exists(), 'Completed payload left an incomplete alias')
                require(item['path'] == wanted, 'Original reader interpreted a different incomplete/completed mapping')
            # Next native owner starts only after this actual original Core has
            # stopped; its baseline is the newly saved original profile.
        summaries[kind] = {'status': 'PASS_ACTUAL_ORIGINAL_MANAGED_HANDOFF', 'same_profile': str(profile),
            'original_baseline_pieces': 1, 'managed_partial_pieces': 7, 'managed_complete_pieces': 8,
            'actual_original_reloads': 3, 'actual_native_admissions': 2, 'file_count': len(baseline['files']),
            'profile_sha256': {p.relative_to(profile).as_posix(): sha(p) for p in sorted(profile.rglob('*')) if p.is_file()}}
    require(source_map(classes) == retained['file_sha256'], 'Original compiled oracle changed during handoff')
    result = {'status': 'PASS_ACTUAL_JAVA_MANAGED_NATIVE_JAVA', 'source_commit': ORIGINAL,
        'core_class_digest': retained['core_class_digest'], 'retention_sha256': sha(args.retention),
        'driver_sha256': sha(driver), 'capsule_manifest_sha256': sha(HERE / 'manifest.json'), 'cases': summaries,
        'original_core_recompiled': False, 'original_profile_or_process_access': False,
        'scope': 'Generated v1 single/multifile linear same-profile stop/reopen only; original API reload, real payload and checked progress',
        'limitations': ['No v2/hybrid, crash/durability, arbitrary storage/network, whole-client or performance parity claim'],
        'commands': commands, 'logs_sha256': {p.name: sha(p) for p in sorted(work.glob('*.std*'))}}
    (work / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(result['status'])
if __name__ == '__main__':
    main()
