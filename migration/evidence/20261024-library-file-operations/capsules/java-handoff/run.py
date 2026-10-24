#!/usr/bin/env python3
"""Native file operations -> actual retained original GlobalManager/READY readback.

Source-only recipe. Root supplies the batched storage-driver binary. Only the
three small fixture helpers are compiled; no original project rebuild or UI.
"""
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
import tempfile

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
ROOT = Path('/home/yurii/Java/BiglyBT')
NATIVE = ROOT.parent / 'BiglyBTC'
JAVA = Path('/usr/lib/jvm/java-17-openjdk-amd64/bin')
ORIGINAL = 'cb8c6cb9bb97916cbc7e80338a017200ffc3e71f'
MODES = ('relocate', 'relink', 'removeKeep', 'removeDelete')

def need(value, reason):
    if not value: raise ValueError(reason)

def regular(path):
    info = path.lstat()
    need(stat.S_ISREG(info.st_mode) and info.st_nlink == 1 and info.st_uid == os.geteuid(),
         'Expected owned regular fixture input: ' + str(path))
    return info

def sha(path):
    regular(path)
    return hashlib.sha256(path.read_bytes()).hexdigest()

def classes_map(classes):
    result = {}
    for path in sorted(classes.rglob('*')):
        need(not path.is_symlink(), 'Symlink in retained class map')
        if path.is_file(): result[path.relative_to(classes).as_posix()] = sha(path)
    return result

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--driver', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    need(not args.output.exists(), 'Fresh durable output required')
    manifest = json.loads((HERE / 'manifest.json').read_text())
    for relative, digest in manifest['files_sha256'].items():
        need(sha(HERE / relative) == digest, 'Changed handoff recipe: ' + relative)
    for path, digest in manifest['dependencies_sha256'].items():
        need(sha(Path(path)) == digest, 'Changed reviewed dependency: ' + path)
    retention_path = NATIVE / 'build/dependencies/java-oracle-17-20261021/retention.json'
    retained = json.loads(retention_path.read_text())
    need(retained['source_commit'] == ORIGINAL and retained['recompiled'] is False,
         'Unchanged retained original class cohort required')
    classes = Path('/tmp/biglybtc-oracle/classes')
    need(classes_map(classes) == retained['file_sha256'], 'Changed original class/resource map')
    driver = args.driver.resolve(strict=True); driver_hash = sha(driver)
    work = Path(tempfile.mkdtemp(prefix='biglybtc-fileops-oracle-', dir='/tmp')); work.chmod(0o700)
    for name in ('classes', 'empty-sourcepath'): (work / name).mkdir(mode=0o700)
    env = {k: v for k, v in os.environ.items() if k not in
           ('CLASSPATH', 'JAVA_TOOL_OPTIONS', '_JAVA_OPTIONS', 'JDK_JAVA_OPTIONS', 'LD_PRELOAD')}
    env['PYTHONDONTWRITEBYTECODE'] = '1'
    commands, cases, summaries = [], {}, []
    def run(name, argv, cwd, timeout):
        row = {'name': name, 'argv': list(map(str, argv)), 'cwd': str(cwd),
               'timeout_seconds': timeout, 'state': 'started'}
        commands.append(row)
        (work / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
        with (work / (name + '.stdout')).open('xb') as out, (work / (name + '.stderr')).open('xb') as err:
            child = subprocess.run(row['argv'], cwd=cwd, env=env, stdout=out, stderr=err, timeout=timeout)
        row.update(state='finished', exit_code=child.returncode)
        (work / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
        need(child.returncode == 0, name + ' failed; actual raw logs retained')
        return (work / (name + '.stdout')).read_text(errors='replace')
    try:
        sources = [ROOT / 'migration/fixtures/java/ReferenceFence.java',
                   ROOT / 'migration/fixtures/java/EngineOracle.java', HERE / 'FileOperationsOracle.java']
        run('javac-fixtures', [JAVA / 'javac', '-J-XX:ActiveProcessorCount=2', '--release', '8',
            '-encoding', 'UTF-8', '-proc:none', '-implicit:none', '-sourcepath', work / 'empty-sourcepath',
            '-cp', classes, '-d', work / 'classes', *sources], work, 90)
        helper_classes = classes_map(work / 'classes')
        need(set(helper_classes) == {'ReferenceFence.class', 'EngineOracle.class',
             'EngineOracle$1.class', 'FileOperationsOracle.class'},
             'Unexpected compilation outside three authored fixture sources')
        sys.path.insert(0, str(ROOT / 'migration/tools'))
        from peer_interop import jvm, unused_port
        from handoff import decode
        classpath = str(work / 'classes') + ':' + str(classes)
        def decoded(path):
            regular(path)
            raw = path.read_bytes(); need(len(raw) <= 4 * 1024 * 1024, 'Generated document bound')
            if raw.startswith(b'\x1f\x8b'): raw = gzip.decompress(raw)
            need(len(raw) <= 4 * 1024 * 1024, 'Generated decoded document bound')
            return decode(raw)
        def rows(document):
            result = {}
            for index, row in enumerate(document[b'downloads']):
                identity = row[b'torrent_hash'].hex().upper()
                need(identity not in result, 'Duplicate generated library hash')
                result[identity] = (index, row)
            return result
        def path(value, case):
            result = Path(value)
            need(result.is_absolute() and result.resolve() == result and result.is_relative_to(case),
                 'Fixture path escaped canonical owned case')
            return result
        def physical(value):
            stamp = regular(value)
            return {'device': stamp.st_dev, 'inode': stamp.st_ino, 'bytes': stamp.st_size, 'sha256': sha(value)}
        def parse_observations(text):
            observed, values = {}, {}
            counts = [int(line.split('\t')[1]) for line in text.splitlines() if line.startswith('ROWS\t')]
            need(len(counts) == 1, 'Missing actual original cardinality')
            for line in text.splitlines():
                row = line.split('\t'); kind = row[0]
                if kind == 'ROW':
                    need(len(row) == 8 and row[1] not in observed and row[3] in ('true','false') and row[4] in ('true','false'), 'Malformed original row')
                    observed[row[1]] = {'state': int(row[2]), 'force': row[3] == 'true', 'paused': row[4] == 'true',
                        'position': int(row[5]), 'received': int(row[6]), 'sent': int(row[7]), 'files': {}}
                elif kind == 'PIECES':
                    need(len(row) == 4 and row[1] in observed and 'pieces' not in observed[row[1]], 'Malformed checked pieces')
                    observed[row[1]]['pieces'] = [int(row[2]), int(row[3])]
                elif kind == 'FILE':
                    need(len(row) == 9 and row[1] in observed and row[4] in ('true','false'), 'Malformed checked file')
                    index = int(row[2]); need(index not in observed[row[1]]['files'], 'Duplicate checked file')
                    observed[row[1]]['files'][index] = {'priority': int(row[3]), 'skipped': row[4] == 'true',
                        'downloaded': int(row[5]), 'bytes': int(row[6]),
                        'path': bytes.fromhex(row[7]).decode('utf-8'), 'sha256': row[8].lower()}
                elif kind == 'VALUE':
                    need(len(row) == 5, 'Malformed original unknown-value observation')
                    key = (row[1], row[2], bytes.fromhex(row[3])); need(key not in values, 'Duplicate original value')
                    value, _ = decode(bytes.fromhex(row[4])); need(set(value) == {b'value'}, 'Unexpected value wrapper')
                    values[key] = value[b'value']
            need(len(observed) == counts[0] and 'FILE_OPERATIONS_ORIGINAL_READBACK_PASS' in text,
                 'Actual original GlobalManager readback incomplete')
            return observed, values
        for mode in MODES:
            case = Path(tempfile.mkdtemp(prefix='biglybtc-fileops-handoff-' + mode + '-', dir='/tmp'))
            case.chmod(0o700); cases[mode] = case
            run(mode + '-native', [driver, case, mode, 'multi'], case, 120)
            receipt = json.loads((case / 'result.json').read_text())
            need(receipt['mode'] == mode and receipt['geometry'] == 'multi' and receipt['schema'] == 1, 'Wrong native receipt')
            profile = path(receipt['profile'], case); need(profile == case / 'profile', 'Wrong generated profile')
            target, survivor = receipt['targetHash'].upper(), receipt['survivorHash'].upper()
            before, before_spans = decoded(case / 'before-downloads.config')
            after, after_spans = decoded(profile / 'downloads.config')
            old, current = rows(before), rows(after)
            removal = mode.startswith('remove')
            need(set(before) == set(after), 'Native changed inventory root shape')
            need(set(old) == {target, survivor} and set(current) == ({survivor} if removal else {target, survivor}), 'Wrong actual native inventory membership')
            expected_pauses = [entry for entry in before.get(b'pause_data', [])
                               if not removal or entry[b'hash'].hex().upper() != target]
            need(after.get(b'pause_data', []) == expected_pauses, 'Native changed unrelated original pause ownership')
            for key in before:
                if key not in (b'downloads', b'pause_data'):
                    need(before_spans[(key,)] == after_spans[(key,)], 'Native changed unknown inventory root bytes')
            for identity, (index, row) in current.items():
                old_index, old_row = old[identity]
                if identity == survivor:
                    need(before_spans[(b'downloads', old_index)] == after_spans[(b'downloads', index)], 'Native altered survivor row')
                else:
                    need(set(row) == set(old_row), 'Native changed selected row shape')
                    for key in row:
                        if key not in (b'save_dir', b'save_file'):
                            need(before_spans[(b'downloads', old_index, key)] == after_spans[(b'downloads', index, key)], 'Native changed selected intent/unknown policy bytes')
            states = {}
            for identity, basename in ((target, 'before-target.dat'), (survivor, 'before-survivor.dat')):
                initial, initial_spans = decoded(case / basename)
                state_path = profile / 'active' / (identity + '.dat')
                if removal and identity == target:
                    need(not state_path.exists(), 'Removed target active state remains'); continue
                actual, actual_spans = decoded(state_path); states[identity] = actual
                need(initial_spans[(b'info',)] == actual_spans[(b'info',)], 'Native changed original info bytes')
                need(initial_spans[(b'attributes', b'opaque.state')] == actual_spans[(b'attributes', b'opaque.state')]
                     and initial_spans[(b'unknown.future.plugin',)] == actual_spans[(b'unknown.future.plugin',)], 'Native changed opaque state bytes')
                if identity == survivor: need(initial == actual, 'Native changed survivor metadata')
                else: need(actual[b'resume'][b'data'][b'valid'] == 0, 'Moved/relinked state does not force original recheck')
            expected_files = {}
            payload_before_java = {}
            for file in receipt['files']:
                index = int(file['index']); need(index not in expected_files, 'Duplicate native file index')
                source = path(file['source'], case)
                expected = path(file['expected'], case)
                need(sha(expected) == file['sha256'] and expected.stat().st_size == file['bytes'], 'Actual expected sidecar mismatch')
                if mode == 'relocate': need(not source.exists(), 'Relocate retained an old source leaf')
                elif mode == 'removeDelete': need(not source.exists(), 'Delete retained an approved leaf')
                else:
                    need(sha(source) == file['sha256'] and source.stat().st_size == file['bytes'],
                         'Keep/relink modified actual source content')
                    # Relink previews describe the existing destination inode;
                    # RemoveKeep describes the source. They are distinct files.
                    if mode == 'removeKeep':
                        need(source.stat().st_ino == int(file['inode']) and source.stat().st_dev == int(file['device']),
                             'Keep modified the previewed source identity')
                    payload_before_java[str(source)] = physical(source)
                if not removal:
                    destination = path(file['destination'], case)
                    need(sha(destination) == file['sha256'] and destination.stat().st_size == file['bytes'], 'Wrong actual destination bytes')
                    if mode == 'relink':
                        need(destination.stat().st_ino == int(file['inode']) and destination.stat().st_dev == int(file['device']),
                             'Relink modified the previewed destination identity')
                    expected_files[index] = {'path': str(destination), 'bytes': file['bytes'], 'sha256': file['sha256']}
                    payload_before_java[str(destination)] = physical(destination)
            survivor_path = case / 'survivor/survivor.bin'
            survivor_file = physical(survivor_path); payload_before_java[str(survivor_path)] = survivor_file
            native_documents = {str(p.relative_to(profile)): sha(p) for p in sorted(profile.rglob('*')) if p.is_file()}
            listen = unused_port()
            actual_text = run(mode + '-java', [*jvm(case, classpath, (listen,)), 'FileOperationsOracle', listen], case, 150)
            observed, values = parse_observations(actual_text)
            need(set(observed) == set(current), 'Original GlobalManager loaded removed/wrong hash')
            paused = {entry[b'hash'].hex().upper() for entry in after.get(b'pause_data', [])}
            for identity, item in observed.items():
                row = current[identity][1]
                need(item['state'] == 70 and item['force'] == (row.get(b'forceStart', 0) != 0)
                     and item['paused'] == (identity in paused), 'Actual original saved intent differs')
                need(item['received'] == row[b'downloaded'] and item['sent'] == row[b'uploaded'], 'Actual original restored counters differ')
                info = states[identity][b'info']; piece_count = len(info[b'pieces']) // 20
                need(item['pieces'] == [piece_count, piece_count], 'Actual original READY did not verify all payload pieces')
                files = expected_files if identity == target else {0: {'path': str(survivor_path), 'bytes': survivor_file['bytes'], 'sha256': survivor_file['sha256']}}
                need(set(item['files']) == set(files), 'Original mapped file count/index differs')
                for index, observed_file in item['files'].items():
                    wanted = files[index]
                    need(all(observed_file[key] == wanted[key] for key in ('path','bytes','sha256')),
                         'Actual original mapped different path or bytes')
                    need(observed_file['priority'] == 0 and not observed_file['skipped']
                         and observed_file['downloaded'] == wanted['bytes'], 'Original normal selected file policy/checked bytes differ')
                for scope, key in (('attributes', b'opaque.state'), ('root', b'unknown.future.plugin')):
                    expected_value = states[identity][b'attributes'][key] if scope == 'attributes' else states[identity][key]
                    need(values.get((identity, scope, key)) == expected_value, 'Original active state lost unknown metadata value')
            ordered = sorted(current, key=lambda identity: current[identity][1][b'position'])
            need(sorted(observed, key=lambda identity: observed[identity]['position']) == ordered,
                 'Original changed relative order of surviving torrents')
            need(payload_before_java == {name: physical(Path(name)) for name in payload_before_java}, 'Original READY changed payload content/inode')
            if mode in ('relocate', 'removeDelete'):
                need(all(not Path(file['source']).exists() for file in receipt['files']),
                     'Original reload resurrected an old moved/deleted source leaf')
            summaries.append({'mode': mode, 'profile': str(profile), 'target': target, 'survivor': survivor,
                'native_receipt_sha256': sha(case / 'result.json'), 'actual_original_hashes': sorted(observed),
                'actual_original_observations': observed, 'native_documents_before_java': native_documents,
                'payload_map_after_java': payload_before_java})
        need(classes_map(classes) == retained['file_sha256'], 'Original class cohort changed during handoff')
        need(sha(driver) == driver_hash, 'Native producer changed during handoff')
        result = {'status': 'PASS_ACTUAL_NATIVE_FILE_OPERATIONS_ORIGINAL_JAVA', 'original_source': ORIGINAL,
            'source_manifest_sha256': sha(HERE / 'manifest.json'), 'native_driver_sha256': driver_hash,
            'retention_sha256': sha(retention_path), 'full_class_map_reference': str(retention_path),
            'core_class_digest': retained['core_class_digest'], 'class_files_and_resources': len(retained['file_sha256']),
            'helper_classes_sha256': helper_classes,
            'class_map_verified_before_and_after': True, 'original_core_recompiled': False,
            'original_profile_or_process_access': False, 'public_network_access': False,
            'scope': 'Generated v1 relocation/relink/removal and actual original GlobalManager READY; no engine continuation or whole-client parity',
            'cases': summaries, 'commands': commands,
            'logs_sha256': {p.name: sha(p) for p in sorted(work.glob('*.std*'))}}
        (work / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
        print(result['status'])
    finally:
        # Keep failed/partial producer and Java data just as actual success data.
        for mode, case in cases.items(): shutil.copytree(case, work / ('case-' + mode))
        args.output.parent.mkdir(parents=True, exist_ok=True)
        shutil.copytree(work, args.output)
        print('Retained file-operation handoff:', args.output)

if __name__ == '__main__':
    main()
