#!/usr/bin/env python3
"""Actual original linear8 -> native compact -> original READY -> native7/8.
Only the new thin Java orchestrator is compiled; prior helper/core classes are reused.
"""
import argparse
import gzip
import importlib.util
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys

sys.dont_write_bytecode = True
AMENDMENT = Path(__file__).resolve().parent
HERE = AMENDMENT.parents[1]
JAVA = Path('/usr/lib/jvm/java-17-openjdk-amd64/bin')
ORIGINAL = 'cb8c6cb9bb97916cbc7e80338a017200ffc3e71f'


def require(okay, reason):
    if not okay:
        raise ValueError(reason)


def sha(path):
    import hashlib
    return hashlib.sha256(path.read_bytes()).hexdigest()


def regular(path):
    info = path.lstat()
    require(stat.S_ISREG(info.st_mode) and info.st_nlink == 1 and info.st_uid == os.geteuid(),
            'Owned regular input required: ' + str(path))
    return info


def file_map(root):
    result = {}
    for path in sorted(root.rglob('*')):
        require(not path.is_symlink(), 'Symlink in bounded source/class/fixture tree')
        if path.is_file():
            regular(path)
            result[path.relative_to(root).as_posix()] = sha(path)
    return result


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference-root', type=Path, required=True)
    parser.add_argument('--classes', type=Path, default=Path('/tmp/biglybtc-oracle/classes'))
    parser.add_argument('--retention', type=Path, default=Path('/home/yurii/Java/BiglyBTC/build/dependencies/java-oracle-17-20261021/retention.json'))
    parser.add_argument('--compaction-driver', type=Path, required=True)
    parser.add_argument('--managed-driver', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    args = parser.parse_args()
    amendment = json.loads((AMENDMENT / 'manifest.json').read_text())
    require(sha(AMENDMENT / 'run.py') == amendment['runner_sha256']
            and sha(HERE / 'manifest.json') == amendment['baseline_manifest_sha256'], 'Changed READY-counter amendment/source basis')
    manifest = json.loads((HERE / 'manifest.json').read_text())
    for name, expected in manifest['files'].items():
        require(sha(HERE / name) == expected, 'Changed compaction recipe/source: ' + name)
    provenance = json.loads((HERE / 'provenance.json').read_text())
    reference = args.reference_root.resolve(strict=True)
    for name, expected in provenance['reference_files'].items():
        require(sha(reference / name) == expected, 'Changed reused fixture/source receipt: ' + name)
    original_receipt = json.loads((reference / provenance['helper_receipt']).read_text())
    require(original_receipt['status'] == 'PASS_ACTUAL_ORIGINAL_STORAGE_HANDOFF'
            and original_receipt['original_commit'] == ORIGINAL, 'Original helper execution basis missing')
    helper_root = reference / provenance['helper_classes']
    require(file_map(helper_root) == original_receipt['authored_class_sha256'], 'Retained thin helpers changed')
    require(sha(args.retention) == provenance['retention_sha256'], 'Changed retained original cohort receipt')
    retained = json.loads(args.retention.read_text())
    require(retained['source_commit'] == ORIGINAL and retained['recompiled'] is False, 'Wrong original cohort')
    classes = args.classes.resolve(strict=True)
    require(classes == args.classes and classes.is_relative_to('/tmp'), 'Canonical retained /tmp classes required')
    require(file_map(classes) == retained['file_sha256'], 'Original classes/resources changed')
    drivers = {}
    for key in ('compaction_driver', 'managed_driver'):
        path = getattr(args, key).resolve(strict=True)
        regular(path); drivers[key] = path
    work = args.work.absolute()
    require(work.parent == Path('/tmp') and work.name.startswith('biglybtc-managed-handoff-storage-compaction-')
            and work.resolve() == work and not work.exists() and not work.is_symlink(), 'Fresh private generated work root required')
    work.mkdir(mode=0o700)
    shutil.copytree(helper_root, work / 'classes')
    (work / 'empty-sourcepath').mkdir(mode=0o700)
    classpath = str(work / 'classes') + os.pathsep + str(classes)
    env = {key: value for key, value in os.environ.items()
           if key not in ('CLASSPATH', 'JAVA_TOOL_OPTIONS', '_JAVA_OPTIONS', 'JDK_JAVA_OPTIONS', 'LD_PRELOAD')}
    env['PYTHONDONTWRITEBYTECODE'] = '1'
    commands = []
    result = {'status': 'RUNNING', 'original_commit': ORIGINAL, 'source_manifest_sha256': sha(HERE / 'manifest.json'),
        'runner_amendment_sha256': sha(AMENDMENT / 'manifest.json'),
        'retention_sha256': sha(args.retention), 'prior_actual_helper_receipt_sha256': sha(reference / provenance['helper_receipt']),
        'original_core_recompiled': False, 'old_fixture_helpers_recompiled': False,
        'original_profile_or_process_access': False,
        'drivers': {name: {'path': str(path), 'sha256': sha(path)} for name, path in drivers.items()}}
    def receipt():
        result['commands'] = commands
        result['logs_sha256'] = {p.name: sha(p) for p in sorted(work.glob('*.std*'))}
        (work / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    receipt()
    def run(name, argv, cwd, timeout):
        command = {'name': name, 'argv': list(map(str, argv)), 'cwd': str(cwd), 'timeout': timeout}
        commands.append(command); (work / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
        with (work / (name + '.stdout')).open('wb') as stdout, (work / (name + '.stderr')).open('wb') as stderr:
            process = subprocess.run(command['argv'], cwd=cwd, env=env, stdout=stdout, stderr=stderr, timeout=timeout)
        require(process.returncode == 0, 'Actual helper failed; raw logs retained: ' + name)
        return (work / (name + '.stdout')).read_text()
    try:
        run('compile-new-wrapper-only', [JAVA / 'javac', '-J-XX:ActiveProcessorCount=2', '--release', '8', '-encoding', 'UTF-8',
            '-proc:none', '-implicit:none', '-sourcepath', work / 'empty-sourcepath', '-cp', classpath,
            '-d', work / 'classes', HERE / 'LinearCompactionOracle.java'], work, 60)
        require(all(sha(work / 'classes' / name) == expected for name, expected in original_receipt['authored_class_sha256'].items()),
                'New wrapper compilation replaced a retained old helper')
        foundation = load('compaction_storage_basis', reference / 'build/storage-20261025/oracle/run.py')
        sys.path.insert(0, str(reference / 'migration/tools'))
        peers = load('compaction_peer_interop', reference / 'migration/tools/peer_interop.py')
        handoff = load('compaction_bencode_spans', reference / 'migration/tools/handoff.py')
        def decoded(path):
            raw = path.read_bytes()
            if raw.startswith(b'\x1f\x8b'):
                raw = gzip.decompress(raw)
            require(len(raw) <= 4 * 1024 * 1024, 'Generated document exceeds fixture bound')
            return handoff.decode(raw)
        def java(name, case, mode, done, kind):
            port = peers.unused_port()
            text = run(name, [*peers.jvm(case, classpath, (port,)), 'LinearCompactionOracle', port, mode, done, kind], case, 180)
            return foundation.profile_rows(text)
        native_case, java_case = work / 'native-case', work / 'java-reference'
        native_case.mkdir(mode=0o700); java_case.mkdir(mode=0o700)
        linear = java('native-input-linear8', native_case, 'prepare-linear', 8, 1)
        original_linear = java('reference-input-linear8', java_case, 'prepare-linear', 8, 1)
        require(linear['pieces'] == original_linear['pieces'] == (8, 8, '11111111')
                and linear['hash'] == original_linear['hash'], 'Inputs must be actual fully checked identical original torrents')
        require(linear['intent'] == original_linear['intent'] and linear['counters'] == original_linear['counters'],
                'Actual linear twins must begin with identical intent and lifetime counters')
        original_compact = java('reference-original-compaction', java_case, 'original-compact', 3, 2)
        require(original_compact['pieces'] == (3, 8, '10000011'), 'Source-derived compact baseline differs')
        require(original_compact['intent'] == original_linear['intent'], 'Original compaction changed unrelated user intent')
        state = native_case / 'profile/active' / (linear['hash'] + '.dat')
        before_state, before_spans = decoded(state)
        opaque = (b'native.storage.opaque',)
        require(opaque in before_spans, 'Original opaque metadata missing')
        before_inventory, _ = decoded(native_case / 'profile/downloads.config')
        before_row = before_inventory[b'downloads'][0]
        untouched_payload = {index: sha(Path(linear['files'][index]['path'])) for index in (0, 2)}
        config = (native_case / 'profile/biglybt.config').read_bytes()
        native_output = native_case / 'native-compact'
        run('native-stopped-compaction', [drivers['compaction_driver'], work, native_case / 'profile', native_output], native_case, 120)
        compact_receipt = json.loads((native_output / 'result.json').read_text())
        require(compact_receipt['status'] == 'COMPACTED_ACTUAL_STOPPED_PROFILE' and compact_receipt['hash'].upper() == linear['hash'].upper()
                and compact_receipt['index'] == 1 and compact_receipt['pending'] is False and compact_receipt['engineOwnerCreated'] is False,
                'Compaction did not finish its actual stopped held-lease transaction')
        require((native_case / 'profile/biglybt.config').read_bytes() == config, 'Compaction changed original global settings')
        after_inventory, _ = decoded(native_case / 'profile/downloads.config')
        after_row = after_inventory[b'downloads'][0]
        for key, value in before_row.items():
            if key not in (b'file_priorities', b'file_priorities_c', b'completedbytes'):
                require(after_row.get(key) == value, 'Compaction changed lifetime/intent/unknown row: ' + repr(key))
        require({key: value for key, value in before_inventory.items() if key != b'downloads'}
                == {key: value for key, value in after_inventory.items() if key != b'downloads'}, 'Unrelated original inventory fields changed')
        after_state, after_spans = decoded(state)
        require(after_spans.get(opaque) == before_spans[opaque], 'Compaction rewrote opaque original metadata')
        require(after_state[b'resume'][b'data'][b'valid'] == 0 and after_state[b'resume'][b'data'][b'resume data'] == b'\x02' * 8
                and after_state[b'attributes'][b'filedownloaded'][b'downloaded'][1] == 0,
                'Discarded data must be unverified before original reload')
        actual_path = Path(compact_receipt['payloadPath'])
        reference_path = Path(original_compact['files'][1]['path'])
        require(actual_path.is_relative_to(native_case / 'payload') and actual_path.resolve(strict=True) == actual_path,
                'Actual compact file escaped generated payload or has a symbolic path')
        require(reference_path.is_relative_to(java_case / 'payload') and reference_path.resolve(strict=True) == reference_path,
                'Original compact file escaped generated payload or has a symbolic path')
        regular(actual_path); regular(reference_path)
        require(all(sha(Path(linear['files'][index]['path'])) == expected for index, expected in untouched_payload.items()),
                'Compaction changed an adjacent selected original file')
        require(actual_path.read_bytes() == reference_path.read_bytes(), 'Native packed bytes differ from actual original L-to-C conversion')
        require(compact_receipt['previousPhysicalBytes'] == 103304 and compact_receipt['physicalBytes'] == actual_path.stat().st_size
                and actual_path.stat().st_size == reference_path.stat().st_size, 'Actual compact physical size differs')
        native_control = native_case / 'profile/active' / linear['hash'] / 'fmfile1.dat'
        reference_control = java_case / 'profile/active' / original_compact['hash'] / 'fmfile1.dat'
        control, _ = decoded(native_control); reference_ctl, _ = decoded(reference_control)
        require(control[b'version'] == reference_ctl[b'version'] == 0 and control[b'length'] == reference_ctl[b'length'] == 103304,
                'Original compact control semantics differ')
        observed = java('native-compact-original-ready', native_case, 'observe-compact', 3, 2)
        # Pinned DownloadManagerController READY legitimately initializes both
        # zero lifetime totals from checked progress. Compare the actual original
        # compacted twin; native preservation was checked before this Java run.
        require(observed['pieces'] == original_compact['pieces'] and observed['intent'] == linear['intent']
                and observed['counters'] == original_compact['counters'], 'Actual Java compact reload differs from original twin checked pieces/intent/counters')
        for index in range(3):
            require({key: value for key, value in observed['files'][index].items() if key != 'path'}
                    == {key: value for key, value in original_compact['files'][index].items() if key != 'path'},
                    'Actual original compact file facts differ at index ' + str(index))
        result['linear'] = linear; result['actual_original_compact_reference'] = original_compact
        result['native_compaction'] = compact_receipt; result['original_native_compact_reload'] = observed
        result['control_sha256'] = {'native': sha(native_control), 'original_reference': sha(reference_control)}
        result['packed_payload_sha256'] = {'native': sha(actual_path), 'original_reference': sha(reference_path)}
        cycle = []
        for mode, expected_done in (('partial', 7), ('complete', 8)):
            # Each actual Java invocation legitimately saves its own loopback
            # port config. Freeze the current bytes separately for each native phase.
            config = (native_case / 'profile/biglybt.config').read_bytes()
            inventory, _ = decoded(native_case / 'profile/downloads.config'); previous = inventory[b'downloads'][0]
            output = native_case / ('native-' + mode)
            run('native-' + mode, [drivers['managed_driver'], work, native_case / 'profile', native_case / 'input.torrent',
                native_case / 'expected/layout', output, mode], native_case, 120)
            value = json.loads((output / 'result.json').read_text())
            require(value['hash'].upper() == linear['hash'].upper() and value['mode'] == mode and value['pieces'] == expected_done
                    and value['session_joined'] is True and value['storage_released'] is True and int(value['received']) > 0,
                    'Missing actual managed transfer/retirement proof')
            require((native_case / 'profile/biglybt.config').read_bytes() == config, 'Native transfer changed global config')
            after, _ = decoded(native_case / 'profile/downloads.config'); row = after[b'downloads'][0]
            require(row[b'downloaded'] == previous.get(b'downloaded', 0) + int(value['received'])
                    and row[b'uploaded'] == previous.get(b'uploaded', 0) + int(value['sent'])
                    and row[b'completedbytes'] == expected_done * 16384, 'Actual checkpoint delta/progress differs')
            require({key: value for key, value in inventory.items() if key != b'downloads'}
                    == {key: value for key, value in after.items() if key != b'downloads'}, 'Managed cycle changed unrelated inventory fields')
            mutable = {b'file_priorities', b'file_priorities_c', b'downloaded', b'uploaded', b'completedbytes', b'allocated',
                       b'secondsDownloading', b'secondsOnlySeeding'}
            for key, prior in previous.items():
                if key not in mutable:
                    require(row.get(key) == prior, 'Managed cycle changed saved intent/unknown: ' + repr(key))
            for key in (b'secondsDownloading', b'secondsOnlySeeding'):
                require(isinstance(row.get(key, 0), int) and row.get(key, 0) >= previous.get(key, 0), 'Accumulated original time regressed')
            _, spans = decoded(state); require(spans.get(opaque) == before_spans[opaque], 'Managed cycle changed opaque original metadata')
            observed = java('original-after-' + mode, native_case, 'observe-linear', expected_done, 1)
            require(observed['intent'] == linear['intent'] and observed['counters'] == (row[b'downloaded'], row[b'uploaded'])
                    and all(not item['skipped'] and item['priority'] == 0 for item in observed['files'].values()),
                    'Actual original reload did not recover native intent/counters/normal selection')
            cycle.append({'mode': mode, 'native': value, 'original': observed})
        result['managed_cycle'] = cycle
        require(file_map(classes) == retained['file_sha256'], 'Original class cohort changed during handoff')
        require(file_map(helper_root) == original_receipt['authored_class_sha256'], 'Old helper cohort changed')
        result['status'] = 'PASS_ACTUAL_ORIGINAL_NATIVE_COMPACTION_HANDOFF'
        result['wrapper_class_sha256'] = {name: value for name, value in file_map(work / 'classes').items() if name.startswith('LinearCompactionOracle')}
        result['generated_profile_sha256'] = file_map(native_case / 'profile')
        result['limitations'] = ['Generated v1 multifile only; stopped explicit transaction; no user-facing compaction command yet',
            'No original core/helper rebuild, public network, real profile or crash/power-loss qualification']
        receipt(); print(result['status'])
    except BaseException as error:
        result['status'] = 'FAIL'; result['failure'] = str(error); receipt(); raise


if __name__ == '__main__':
    main()
