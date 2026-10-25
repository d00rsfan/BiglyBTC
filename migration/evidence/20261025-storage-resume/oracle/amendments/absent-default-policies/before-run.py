#!/usr/bin/env python3
"""Bounded actual original FMFile and same-profile managed storage handoffs.

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


def prepare_tape(root, kind):
    """Literal input operations; expected layout is produced by original Java."""
    root.mkdir(mode=0o700)
    pattern = bytes((i * 37 + i // 127 + 19) & 255 for i in range(LENGTH))
    first = 13312
    starts = [0] + [first + (i - 1) * PIECE for i in range(1, 7)]
    for index, start in enumerate(starts):
        end = first if index == 0 else min(start + PIECE, LENGTH)
        (root / ('fragment-' + str(index) + '.bin')).write_bytes(pattern[start:end])
    lines = [f'OPEN\t{kind}\t{OFFSET}\t{LENGTH}\t{PIECE}',
             'WRITE\t0\tfragment-0.bin', 'WRITE\t95232\tfragment-6.bin',
             'WRITE\t29696\tfragment-2.bin', f'LENGTH\t{LENGTH}',
             'READ\t13312\t16384\tmissing-one']
    if kind != 2:
        lines += ['WRITE\t13312\tfragment-1.bin', 'COMPLETE\t1\tfragment-1.bin']
    lines += ['FLUSH', 'CLOSE', 'REOPEN', 'READ\t0\t13312\tfirst',
              'READ\t95232\t8072\tlast']
    if kind != 2:
        lines += ['READ\t13312\t16384\tone', 'READ\t29696\t16384\ttwo',
                  'WRITE\t46080\tfragment-3.bin', 'WRITE\t62464\tfragment-4.bin',
                  'WRITE\t78848\tfragment-5.bin', 'COMPLETE\t5\tfragment-5.bin',
                  'FLUSH', 'CLOSE', 'REOPEN', f'READ\t0\t{LENGTH}\twhole']
    lines += ['FLUSH', 'CLOSE']
    (root / 'request.tsv').write_text('\n'.join(lines) + '\n')
    return pattern


def read_tape(root, kind, label):
    lines = [f'OPEN\t{kind}\t{OFFSET}\t{LENGTH}\t{PIECE}']
    if kind == 2:
        lines += [f'READ\t0\t13312\t{label}-first', f'READ\t95232\t8072\t{label}-last']
    else:
        lines += [f'READ\t0\t{LENGTH}\t{label}-whole']
    lines += ['CLOSE']
    tape = root / (label + '.tsv'); tape.write_text('\n'.join(lines) + '\n')
    return tape


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
    parser.add_argument('--codec-driver', type=Path)
    parser.add_argument('--managed-driver', type=Path)
    parser.add_argument('--phase', choices=('codec', 'managed', 'all'), default='all')
    parser.add_argument('--work', type=Path, required=True)
    args = parser.parse_args()
    manifest = json.loads((HERE / 'manifest.json').read_text())
    for name, digest in manifest['files'].items():
        require(sha(HERE / name) == digest, 'Changed source recipe: ' + name)
    provenance = json.loads((HERE / 'source-provenance.json').read_text())
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
    if args.phase in ('codec', 'all'):
        require(args.codec_driver is not None, 'Codec phase requires actual native codec driver')
    if args.phase in ('managed', 'all'):
        require(args.managed_driver is not None, 'Managed phase requires actual native mapped-storage owner driver')
    drivers = {}
    for key in ('codec_driver', 'managed_driver'):
        if getattr(args, key) is not None:
            path = getattr(args, key).resolve(strict=True); regular(path); drivers[key] = path
    work = args.work.absolute()
    require(work.parent == Path('/tmp') and work.name.startswith('biglybtc-managed-handoff-storage-')
            and work.resolve() == work and not work.exists() and not work.is_symlink(), 'Fresh private owned test root required')
    work.mkdir(mode=0o700); (work / 'classes').mkdir(mode=0o700); (work / 'empty-sourcepath').mkdir(mode=0o700)
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
    sources += [HERE / 'StorageScriptOracle.java', HERE / 'StorageProfileOracle.java']
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
        'source_provenance_sha256': sha(HERE / 'source-provenance.json'), 'manifest_sha256': sha(HERE / 'manifest.json'),
        'retention_sha256': sha(args.retention), 'original_core_recompiled': False,
        'original_profile_or_process_access': False, 'drivers': {key: {'path': str(path), 'sha256': sha(path)} for key, path in drivers.items()}}
    (work / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    if args.phase in ('codec', 'all'):
        inherited = work / 'inherited-original'; inherited.mkdir(mode=0o700)
        java('inherited-seven-layouts', inherited, 'StorageOracle', inherited / 'fixtures')
        inherited_checked = {}
        for fixture in sorted((inherited / 'fixtures').iterdir()):
            geometry, _ = decoded(fixture / 'layout.config')
            kind, offset, length = (geometry[key] for key in (b'type', b'offset', b'length'))
            known = (fixture / 'present.bin').read_bytes()
            logical = (fixture / 'logical.bin').read_bytes()
            require(len(logical) == length and len(known) == geometry[b'pieces'], 'Bad actual inherited original geometry')
            before = file_map(fixture)
            first = min(length, PIECE - offset % PIECE)
            lines = [f'OPEN\t{kind}\t{offset}\t{length}\t{PIECE}']
            ranges = []
            for index in range(len(known)):
                start = 0 if index == 0 else first + (index - 1) * PIECE
                end = first if index == 0 else min(start + PIECE, length)
                label = 'inherited-' + str(index)
                lines.append(f'READ\t{start}\t{end - start}\t{label}')
                ranges.append((index, start, end, label))
            lines.append('CLOSE'); tape = fixture / 'native-read.tsv'; tape.write_text('\n'.join(lines) + '\n')
            run('inherited-' + fixture.name + '-native', [drivers['codec_driver'], fixture, tape], work, 30)
            for index, start, end, label in ranges:
                observation = json.loads((fixture / ('read-' + label + '.json')).read_text())
                if known[index]:
                    require(not observation['missing'] and (fixture / ('read-' + label + '.bin')).read_bytes() == logical[start:end],
                            'Native disagrees with actual inherited original known bytes')
                else:
                    require(observation['missing'], 'Native certified absent inherited original bytes')
            after = file_map(fixture)
            require(all(after.get(name) == digest for name, digest in before.items()), 'Inherited original input changed on native read')
            inherited_checked[fixture.name] = {'original_known_file_pieces': list(known), 'original_sha256': before,
                'native_read_sha256': {name: digest for name, digest in after.items() if name not in before}}
        raw_results = {}
        for kind in (2, 3, 4):
            case = work / ('codec-' + str(kind)); case.mkdir(mode=0o700)
            original, native = case / 'original', case / 'native'
            pattern = prepare_tape(original, kind); prepare_tape(native, kind)
            java('codec-' + str(kind) + '-original', work, 'StorageScriptOracle', original, original / 'request.tsv')
            run('codec-' + str(kind) + '-native', [drivers['codec_driver'], native, native / 'request.tsv'], case, 30)
            labels = ('first', 'last') if kind == 2 else ('first', 'last', 'one', 'two', 'whole')
            for label in labels:
                a, b = original / ('read-' + label + '.bin'), native / ('read-' + label + '.bin')
                require(a.read_bytes() == b.read_bytes(), 'Actual original/native known read differs: ' + str(kind) + '/' + label)
            require((original / 'read-first.bin').read_bytes() == pattern[:13312]
                    and (original / 'read-last.bin').read_bytes() == pattern[95232:], 'Original known written fragments differ')
            if kind != 2:
                require((original / 'read-whole.bin').read_bytes() == pattern, 'Original final logical stream differs from actual inputs')
            missing = json.loads((native / 'read-missing-one.json').read_text())
            require(missing['missing'] == [{'offset': 13312, 'length': 16384}],
                    'Native promoted an original missing allocation/compact hole into data')
            # Cross both physical/control products through the other real reader.
            original_tape = read_tape(original, kind, 'native-read')
            before = {p: file_map(original).get(p) for p in ('payload.bin.az!', 'control/fmfile1.dat', 'control/fmfile1.dat.2')}
            run('codec-' + str(kind) + '-native-reads-java', [drivers['codec_driver'], original, original_tape], case, 30)
            after = file_map(original)
            require(all(after.get(p) == digest for p, digest in before.items()), 'Native read-only reopen changed original payload/control')
            inverse = read_tape(native, kind, 'java-read')
            java('codec-' + str(kind) + '-java-reads-native', work, 'StorageScriptOracle', native, inverse)
            for label in (('first', 'last') if kind == 2 else ('whole',)):
                require((native / ('read-java-read-' + label + '.bin')).read_bytes()
                        == (original / ('read-' + label + '.bin')).read_bytes(), 'Original rejected native written logical/control state')
                require((original / ('read-native-read-' + label + '.bin')).read_bytes()
                        == (original / ('read-' + label + '.bin')).read_bytes(), 'Native rejected original written logical/control state')
            raw_results[str(kind)] = {'status': 'PASS_ACTUAL_BIDIRECTIONAL_FMFILE', 'original': file_map(original), 'native': file_map(native)}
        result['codec'] = raw_results
        result['inherited_original_read_layouts'] = inherited_checked
    if args.phase in ('managed', 'all'):
        profiles = {}
        for kind in (2, 3, 4):
            case = work / ('profile-' + str(kind)); case.mkdir(mode=0o700)
            baseline_done = 3 if kind == 2 else 4
            port = peer_interop.unused_port()
            baseline = profile_rows(java('profile-' + str(kind) + '-prepare', case, 'StorageProfileOracle', port,
                'prepare', kind, baseline_done, ports=(port,), timeout=180))
            require(baseline['files'][0]['skipped'] is False and baseline['files'][2]['skipped'] is False
                    and baseline['files'][1]['skipped'] == (kind in (2, 4)), 'Actual original format/selection semantics differ')
            state = case / 'profile/active' / (baseline['hash'] + '.dat')
            _, before_spans = decoded(state)
            opaque_path = (b'native.storage.opaque',)
            require(opaque_path in before_spans, 'Original did not retain fixture opaque metadata')
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
                else:
                    require(row.get(b'file_priorities') == [-1, -1, -1], 'Native did not persist all three selected normal policies')
                port = peer_interop.unused_port()
                observed = profile_rows(java('profile-' + str(kind) + '-java-' + mode, case, 'StorageProfileOracle', port,
                    'observe', kind, expected_done, ports=(port,), timeout=150))
                require(observed['hash'] == baseline['hash'] and observed['intent'] == baseline['intent'], 'Native changed original saved intent/limits')
                require(all(not item['skipped'] and item['priority'] == 0 for item in observed['files'].values()),
                        'Original reload did not interpret selected normal priorities')
                require(observed['counters'] == (row[b'downloaded'], row[b'uploaded']), 'Original did not restore actual native counters')
                require(observed['files'][1]['kind'] == (1 if kind == 2 else 3), 'Explicit selected storage transition differs')
                observations.append({'mode': mode, 'native': receipt, 'original': observed})
            profiles[str(kind)] = {'baseline': baseline, 'handoffs': observations,
                'profile_files': file_map(case / 'profile')}
        result['managed'] = profiles
    require(file_map(classes) == retained['file_sha256'], 'Original class cohort changed during actual handoff')
    result['status'] = 'PASS_ACTUAL_ORIGINAL_STORAGE_HANDOFF'
    result['authored_class_sha256'] = file_map(work / 'classes')
    result['commands'] = commands
    result['logs_sha256'] = {p.name: sha(p) for p in sorted(work.glob('*.std*'))}
    result['scope'] = 'Generated accepted-domain FMFile and actual v1 managed profile handoffs; missing bytes remain unverified'
    result['limitations'] = ['No original core rebuild, real profile, public network, power-loss, arbitrary-format or whole-client claim']
    (work / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(result['status'])


if __name__ == '__main__':
    main()
