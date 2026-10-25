#!/usr/bin/env python3
"""Actual original packed-layout Move and same-profile managed handoffs.

Compile only five small fixture helpers against the retained original classes.
Never build the original core. Root supplies approved native executables.
"""
import argparse
import gzip
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import stat
import subprocess
import sys

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
BASE = HERE.parent / 'oracle'
ORIGINAL = 'cb8c6cb9bb97916cbc7e80338a017200ffc3e71f'
JAVA = Path('/usr/lib/jvm/java-17-openjdk-amd64/bin')
PIECE = 16384
OFFSET, LENGTH = 3072, 103304


def require(value, reason):
    if not value:
        raise ValueError(reason)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def regular(path):
    info = path.lstat()
    require(stat.S_ISREG(info.st_mode) and info.st_nlink == 1 and info.st_uid == os.geteuid(),
            'Unsafe generated input: ' + str(path))
    return info


def file_map(root):
    result = {}
    for path in sorted(root.rglob('*')):
        require(not path.is_symlink(), 'Symlink in bounded fixture/cohort')
        if path.is_file():
            regular(path)
            result[path.relative_to(root).as_posix()] = sha(path)
    return result


def import_helper(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def profile_rows(text):
    result = {'files': {}}
    for line in text.splitlines():
        if not line.startswith('PROFILE_'):
            continue
        row = line.split('\t')
        if row[0] == 'PROFILE_READY':
            require(len(row) == 4 and 'pieces' not in result, 'Bad actual READY marker')
            result['pieces'] = (int(row[1]), int(row[2]), row[3])
        elif row[0] == 'PROFILE_HASH':
            require(len(row) == 2 and 'hash' not in result, 'Bad actual hash marker')
            result['hash'] = row[1]
        elif row[0] == 'PROFILE_INTENT':
            require(len(row) == 6 and 'intent' not in result, 'Bad actual intent marker')
            result['intent'] = row[1:]
        elif row[0] == 'PROFILE_COUNTERS':
            require(len(row) == 3 and 'counters' not in result, 'Bad actual counter marker')
            result['counters'] = tuple(map(int, row[1:]))
        elif row[0] == 'PROFILE_FILE':
            require(len(row) == 8 and row[3] in ('true', 'false'), 'Bad actual file marker')
            index = int(row[1]); require(index not in result['files'], 'Duplicate actual file')
            result['files'][index] = {'priority': int(row[2]), 'skipped': row[3] == 'true',
                'kind': int(row[4]), 'downloaded': int(row[5]), 'bytes': int(row[6]),
                'path': bytes.fromhex(row[7]).decode('utf-8')}
        elif row[0] == 'PROFILE_ORIGINAL_STOPPED':
            require(len(row) == 1 and 'stopped' not in result, 'Bad actual stopped marker')
            result['stopped'] = True
        else:
            raise ValueError('Unknown actual profile marker')
    require(set(result) == {'pieces', 'hash', 'intent', 'counters', 'files', 'stopped'}
            and set(result['files']) == {0, 1, 2}, 'Incomplete original profile observation')
    require(result['pieces'][1] == 8 and len(result['pieces'][2]) == 8
            and result['pieces'][2].count('1') == result['pieces'][0], 'Bad actual piece bitmap')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference-root', type=Path, required=True)
    parser.add_argument('--classes', type=Path, default=Path('/tmp/biglybtc-oracle/classes'))
    parser.add_argument('--retention', type=Path, default=Path('/home/yurii/Java/BiglyBTC/build/dependencies/java-oracle-17-20261021/retention.json'))
    parser.add_argument('--fileops-driver', type=Path, required=True)
    parser.add_argument('--managed-driver', type=Path, required=True)
    parser.add_argument('--phase', choices=('managed',), default='managed')
    parser.add_argument('--work', type=Path, required=True)
    args = parser.parse_args()
    manifest = json.loads((HERE / 'manifest.json').read_text())
    for name, digest in manifest['files'].items():
        require(sha(HERE / name) == digest, 'Changed source recipe: ' + name)
    for name, digest in manifest['base_files'].items():
        require(sha(BASE / name) == digest, 'Changed original handoff input: ' + name)
    provenance = json.loads((BASE / 'source-provenance.json').read_text())
    reference = args.reference_root.resolve(strict=True)
    for name, digest in provenance['reference_sha256'].items():
        require(sha(reference / name) == digest, 'Changed reused fixture helper: ' + name)
    for name, entry in provenance['original_sources'].items():
        require(sha(reference / name) == entry['sha256'], 'Changed original source: ' + name)
    regular(args.retention)
    require(sha(args.retention) == provenance['java_retention_sha256'], 'Changed Java retention receipt')
    retained = json.loads(args.retention.read_text())
    require(retained['source_commit'] == ORIGINAL and retained['recompiled'] is False, 'Wrong original class cohort')
    classes = args.classes.resolve(strict=True)
    require(classes == args.classes and classes.is_relative_to('/tmp'), 'Guard needs canonical generated /tmp classes')
    require(file_map(classes) == retained['file_sha256'], 'Original compiled classes/resources changed')
    drivers = {}
    for key in ('fileops_driver', 'managed_driver'):
        if getattr(args, key) is not None:
            path = getattr(args, key).resolve(strict=True); regular(path); drivers[key] = path
    work = args.work.absolute()
    require(work.parent == Path('/tmp') and work.name.startswith('biglybtc-managed-handoff-storage-fileops-')
            and work.resolve() == work and not work.exists() and not work.is_symlink(), 'Fresh private owned test root required')
    work.mkdir(mode=0o700)
    (work / '.storage-fileops-fixture').write_text('cf01-owned-nonlinear-move\n')
    (work / 'classes').mkdir(mode=0o700); (work / 'empty-sourcepath').mkdir(mode=0o700)
    classpath = str(work / 'classes') + os.pathsep + str(classes)
    env = {key: value for key, value in os.environ.items()
           if key not in ('CLASSPATH', 'JAVA_TOOL_OPTIONS', '_JAVA_OPTIONS', 'JDK_JAVA_OPTIONS', 'LD_PRELOAD')}
    env['PYTHONDONTWRITEBYTECODE'] = '1'
    commands = []

    def run(name, argv, cwd, timeout):
        command = {'name': name, 'argv': list(map(str, argv)), 'cwd': str(cwd), 'timeout': timeout}
        commands.append(command); (work / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
        with (work / (name + '.stdout')).open('wb') as stdout, (work / (name + '.stderr')).open('wb') as stderr:
            completed = subprocess.run(command['argv'], cwd=cwd, env=env, stdout=stdout, stderr=stderr, timeout=timeout)
        require(completed.returncode == 0, 'Actual helper failed; preserved logs: ' + name)
        return (work / (name + '.stdout')).read_text()

    sources = [reference / 'migration/fixtures/java' / name for name in ('ReferenceFence.java', 'EngineOracle.java', 'StorageOracle.java')]
    sources += [BASE / 'StorageScriptOracle.java', BASE / 'StorageProfileOracle.java']
    run('compile-fixture-helpers', [JAVA / 'javac', '-J-XX:ActiveProcessorCount=2', '--release', '8', '-encoding', 'UTF-8',
        '-proc:none', '-implicit:none', '-sourcepath', work / 'empty-sourcepath', '-cp', classes, '-d', work / 'classes', *sources], work, 60)
    sys.path.insert(0, str(reference / 'migration/tools'))
    peer_interop = import_helper('storage_peer_interop', reference / 'migration/tools/peer_interop.py')
    handoff = import_helper('storage_handoff', reference / 'migration/tools/handoff.py')

    def java(name, root, helper, *arguments, ports=(), timeout=150):
        return run(name, [*peer_interop.jvm(root, classpath, ports), helper, *arguments], root, timeout)

    def decoded(path):
        raw = path.read_bytes()
        if raw.startswith(b'\x1f\x8b'):
            raw = gzip.decompress(raw)
        require(len(raw) <= 4 * 1024 * 1024, 'Unbounded generated state')
        return handoff.decode(raw)

    result = {'status': 'RUNNING', 'phase': args.phase, 'original_commit': ORIGINAL,
        'source_provenance_sha256': sha(BASE / 'source-provenance.json'), 'manifest_sha256': sha(HERE / 'manifest.json'),
        'retention_sha256': sha(args.retention), 'original_core_recompiled': False,
        'original_profile_or_process_access': False, 'drivers': {key: {'path': str(path), 'sha256': sha(path)} for key, path in drivers.items()}}
    (work / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    def move_and_observe(kind, case, previous, stage, expected_done):
        profile = case / 'profile'
        before_inventory, _ = decoded(profile / 'downloads.config')
        _, before_state = decoded(profile / 'active' / (previous['hash'] + '.dat'))
        config_before = (profile / 'biglybt.config').read_bytes()
        output = case / ('native-move-' + stage + '.json')
        run('profile-' + str(kind) + '-native-move-' + stage, [drivers['fileops_driver'],
            work, profile, case / 'payload' / ('moved-' + stage), output], case, 60)
        receipt = json.loads(output.read_text())
        require(receipt['hash'] == previous['hash'] and receipt['files_moved'] == 3
                and receipt['lease_reacquired_after_release'] is True
                and receipt['engine_started'] is False and receipt['config_unchanged'] is True
                and receipt['controls_before'] == receipt['controls_after'],
                'Missing exact packed move/lease/control proof')
        require((profile / 'biglybt.config').read_bytes() == config_before,
                'Relocation changed original global settings')
        inventory, _ = decoded(profile / 'downloads.config')
        require(len(inventory[b'downloads']) == 1, 'Move changed library cardinality')
        for key, value in before_inventory.items():
            if key != b'downloads':
                require(inventory.get(key) == value, 'Move changed unrelated library data')
        before_row, after_row = before_inventory[b'downloads'][0], inventory[b'downloads'][0]
        for key, value in before_row.items():
            if key not in (b'save_dir', b'save_file', b'savePath'):
                require(after_row.get(key) == value, 'Move changed saved intent/counters/unknown: ' + repr(key))
        _, after_state = decoded(profile / 'active' / (previous['hash'] + '.dat'))
        require(after_state.get((b'native.storage.opaque',)) == before_state[(b'native.storage.opaque',)],
                'Move rewrote original opaque state')
        port = peer_interop.unused_port()
        observed = profile_rows(java('profile-' + str(kind) + '-java-move-' + stage, case,
            'StorageProfileOracle', port, 'observe', kind, expected_done, ports=(port,), timeout=150))
        require(observed['hash'] == previous['hash'] and observed['intent'] == previous['intent']
                and observed['counters'] == previous['counters'] and observed['pieces'] == previous['pieces'],
                'Original READY changed actual piece/intent/counter baseline after move')
        entries = {item['index']: item for item in receipt['files']}
        require(set(entries) == {0, 1, 2}, 'Move lost an actual payload leaf')
        for index, old in previous['files'].items():
            current = observed['files'][index]
            require(current['path'] == entries[index]['destination'], 'Original did not restore relocated path')
            require(all(current[key] == old[key] for key in ('priority', 'skipped', 'kind', 'downloaded', 'bytes')),
                    'Relocation changed actual original file policy/progress/geometry')
            require(not Path(entries[index]['source']).exists()
                    and sha(Path(entries[index]['destination'])) == entries[index]['sha256'],
                    'Original READY changed moved physical bytes')
        return {'stage': stage, 'native': receipt, 'original': observed}

    if args.phase in ('managed', 'all'):
        profiles = {}
        for kind in (2, 3, 4):
            case = work / ('profile-' + str(kind)); case.mkdir(mode=0o700)
            baseline_done = 3 if kind in (2, 4) else 4
            port = peer_interop.unused_port()
            baseline = profile_rows(java('profile-' + str(kind) + '-prepare', case, 'StorageProfileOracle', port,
                'prepare', kind, baseline_done, ports=(port,), timeout=180))
            require(baseline['files'][0]['skipped'] is False and baseline['files'][2]['skipped'] is False
                    and baseline['files'][1]['skipped'] == (kind in (2, 4)), 'Actual original format/selection semantics differ')
            state = case / 'profile/active' / (baseline['hash'] + '.dat')
            _, before_spans = decoded(state)
            opaque_path = (b'native.storage.opaque',)
            require(opaque_path in before_spans, 'Original did not retain fixture opaque metadata')
            moves = [move_and_observe(kind, case, baseline, 'before', baseline_done)]
            observations = []
            for mode, expected_done in (('partial', 7), ('complete', 8)):
                original_config = (case / 'profile/biglybt.config').read_bytes()
                before_inventory, _ = decoded(case / 'profile/downloads.config')
                require(len(before_inventory[b'downloads']) == 1, 'Unexpected original library cardinality')
                previous_row = before_inventory[b'downloads'][0]
                output = case / ('native-' + mode)
                run('profile-' + str(kind) + '-native-' + mode, [drivers['managed_driver'], work,
                    case / 'profile', case / 'input.torrent', case / 'expected/layout', output, mode], case, 120)
                receipt = json.loads((output / 'result.json').read_text())
                require(receipt['mode'] == mode and receipt['hash'] == baseline['hash']
                        and receipt['session_joined'] is True and receipt['storage_released'] is True
                        and receipt['pieces'] == expected_done and int(receipt['received']) > 0,
                        'Missing actual managed transfer/retirement proof')
                require((case / 'profile/biglybt.config').read_bytes() == original_config, 'Native changed original global settings')
                _, native_spans = decoded(state)
                require(native_spans.get(opaque_path) == before_spans[opaque_path], 'Native rewrote opaque original metadata')
                inventory, _ = decoded(case / 'profile/downloads.config')
                require(len(inventory[b'downloads']) == 1, 'Unexpected native saved library cardinality')
                row = inventory[b'downloads'][0]
                require(row[b'downloaded'] == previous_row.get(b'downloaded', 0) + int(receipt['received'])
                        and row[b'uploaded'] == previous_row.get(b'uploaded', 0) + int(receipt['sent'])
                        and row[b'completedbytes'] == expected_done * PIECE, 'Actual native checkpoint counters/progress differ')
                for key, value in before_inventory.items():
                    if key != b'downloads':
                        require(inventory.get(key) == value, 'Native changed unrelated original inventory root data')
                mutable = {b'file_priorities', b'file_priorities_c', b'downloaded', b'uploaded', b'completedbytes', b'allocated',
                           b'secondsDownloading', b'secondsOnlySeeding'}
                for key, value in previous_row.items():
                    if key not in mutable:
                        require(row.get(key) == value, 'Native changed original saved intent/unknown field: ' + repr(key))
                for key in (b'secondsDownloading', b'secondsOnlySeeding'):
                    require(isinstance(row.get(key, 0), int) and row.get(key, 0) >= previous_row.get(key, 0),
                            'Native lost original accumulated download/seeding time')
                if b'file_priorities_c' in row:
                    require(row[b'file_priorities_c'] in ({b'-1': b'0-2'}, {b'-1': b'0,1,2'}), 'Native did not persist all three selected normal policies')
                elif b'file_priorities' in row:
                    require(row[b'file_priorities'] == [-1, -1, -1], 'Native did not persist all three selected normal policies')
                # Original saves omit both fields when all files use normal defaults.
                # The actual Java reload below must still report all3 normal/selected.
                port = peer_interop.unused_port()
                observed = profile_rows(java('profile-' + str(kind) + '-java-' + mode, case, 'StorageProfileOracle', port,
                    'observe', kind, expected_done, ports=(port,), timeout=150))
                require(observed['hash'] == baseline['hash'] and observed['intent'] == baseline['intent'], 'Native changed original saved intent/limits')
                require(all(not item['skipped'] and item['priority'] == 0 for item in observed['files'].values()),
                        'Original reload did not interpret selected normal priorities')
                require(observed['counters'] == (row[b'downloaded'], row[b'uploaded']), 'Original did not restore actual native counters')
                require(observed['files'][1]['kind'] == (1 if kind == 2 else 3), 'Explicit selected storage transition differs')
                observations.append({'mode': mode, 'native': receipt, 'original': observed})
            moves.append(move_and_observe(kind, case, observations[-1]['original'], 'after', 8))
            profiles[str(kind)] = {'baseline': baseline, 'moves': moves, 'handoffs': observations,
                'profile_files': file_map(case / 'profile')}
        result['managed'] = profiles
    require(file_map(classes) == retained['file_sha256'], 'Original class cohort changed during actual handoff')
    result['status'] = 'PASS_ACTUAL_ORIGINAL_NONLINEAR_MOVE_HANDOFF'
    result['authored_class_sha256'] = file_map(work / 'classes')
    result['commands'] = commands
    result['logs_sha256'] = {p.name: sha(p) for p in sorted(work.glob('*.std*'))}
    result['scope'] = 'Generated packed C/R/X Move before selection, actual partial/full transfer, second Move and original READY/full bytes'
    result['limitations'] = ['No original core rebuild, real profile, public network, power-loss, arbitrary-format or whole-client claim']
    (work / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(result['status'])


if __name__ == '__main__':
    main()
