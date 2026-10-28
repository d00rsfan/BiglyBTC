#!/usr/bin/env python3
"""Plan source observations only; freeze needs an explicitly compiled reference."""
import argparse
import json
import pathlib
import shutil
import subprocess
import tempfile

from session_export_pause_oracle import (ROOT, EVIDENCE, PLANNED, FIXTURES, files, inputs, sha, source_plan, verify)

STATES = [-1, 0, 5, 10, 20, 30, 40, 50, 55, 60, 65, 70, 71, 75, 100]
HASH = 'x' + 'A5' * 20


def utf(text):
    if text is None:
        return 'N'
    return 'u' + text.encode('utf-16-be', errors='surrogatepass').hex().upper()


def setup(state=50, forced=0, assumed=0, present=True):
    return ['Z', 'T t ' + HASH, f'M m {"t" if present else "N"} {state} {forced} {assumed} 1 1', 'REG t m']


def commands():
    groups = {name: [] for name in ['export-semantic', 'pause-semantic', 'callback-order',
                                    'getter-fault', 'alias-policy', 'provider-boundary']}
    export = groups['export-semantic']
    for state in STATES + [-2**31, 2**31-1]:
        for forced in [0, 1]:
            for assumed in [0, 1]:
                for internal, interim in [(0, 0), (1, 0), (0, 1), (1, 1)]:
                    export += setup(state, forced, assumed) + [f'EXP m {internal} {interim}', 'SNAP']
    for field in ['getTotalDataBytesReceived', 'getTotalDataBytesSent', 'getDownloadCompletedBytes',
                  'getDiscarded', 'getHashFailBytes', 'getSecondsDownloading', 'getSecondsOnlySeeding']:
        for value in [-2**63, -1, 0, 1, 2**63-1]:
            export += setup() + [f'S m {field} {value}', 'EXP m 1 0']
    for field in ['getDownloadRateLimitBytesPerSecond', 'getUploadRateLimitBytesPerSecond']:
        for value in [-2**31, -1, 0, 1, 2**31-1]:
            export += setup() + [f'S m {field} {value}', 'EXP m 1 0']
    for field in ['getPosition', 'getMaxUploads', 'getErrorFlags']:
        for value in [-2**31, -1, 0, 1, 2**31-1]:
            export += setup(100) + [f'V m {field} I{value}', 'V m getErrorType I1', 'EXP m 1 0']
    for error in [0, 1, 2, 3, 4, -1, 2**31-1]:
        for detail in [None, '', 'space detail', 'א/ع', '\ud800']:
            export += setup(100, 1, 1) + [f'V m getErrorType I{error}', 'V m getErrorDetails ' + utf(detail), 'EXP m 0 0']
    for path in ['', '/', '/tmp/generated/file', '/tmp/generated/א/ع', 'relative/file', 'file', '/tmp/generated/../file']:
        export += setup() + ['V m getAbsoluteSaveLocation F' + utf(path), 'EXP m 1 0']
    for priorities in ['J0', 'J-1,J-1,J0,J0,J5,J5', 'J1,J2,J1,J2', 'J-2147483648,J2147483647', 'J-9223372036854775808,J9223372036854775807']:
        export += setup() + ['L p ' + priorities, 'P m p', 'EXP m 1 0']
    for name, token in [('isPersistent', 'B0'), ('isDataAlreadyAllocated', 'B0'),
                        ('getTorrentFileName', utf('non-ascii/א.torrent')), ('getCreationTime', 'J-9223372036854775808')]:
        export += setup() + [f'V m {name} {token}', 'EXP m 1 0']

    pause = groups['pause-semantic']
    for operation in ['ISPAUSED', 'RESUME', 'RESUMING', 'STOPPAUSED']:
        for state in STATES:
            for forced in [0, 1]:
                for paused in ['ABSENT', '0', '1']:
                    for current in ['m', 'impostor', 'N']:
                        rows = setup(state, forced) + [f'M impostor t {state} {forced} 0 1 1', 'REG t ' + current]
                        if paused != 'ABSENT':
                            rows += [f'PS t {paused} 2']
                        pause += rows + [operation + ' m', 'SNAP']
    for state in STATES:
        for forced in [0, 1]:
            for only in [0, 1]:
                for paused in ['ABSENT', '0', '1']:
                    rows = setup(state, forced)
                    if paused != 'ABSENT':
                        rows += ['PS t ' + paused + ' 2']
                    pause += rows + [f'PAUSE m {only}', 'SNAP']
    for operation in ['ISPAUSED m', 'PAUSE m 0', 'PAUSE m 1', 'RESUME m', 'RESUMING m', 'STOPPAUSED m']:
        pause += setup(present=False) + [operation, 'SNAP']
    for owner in [-2**31, -1, 0, 1, 2, 2**31-1]:
        pause += setup(70, 1) + [f'PS t 1 {owner}', 'ISPAUSED m', 'RESUME m', 'SNAP']
    pause += setup(50, 1) + ['PAUSE m 0', 'ISPAUSED m', 'V m getState I70', 'RESUME m', 'SNAP',
                             'PAUSE m 0', 'V m getState I70', 'STOPPAUSED m', 'ISPAUSED m']

    order = groups['callback-order']
    for tag in [0, 1]:
        for state in [50, 60, 70]:
            order += setup(state, 1) + [f'TAG {tag}', 'PAUSE m 0', 'SNAP']
            order += setup(state, 1) + [f'TAG {tag}', 'PS t 1 2', 'STOPPAUSED m', 'SNAP']
    for replacement in ['KEEP', 'N', 'new']:
        for interim in [0, 1]:
            order += setup() + ['L old J1,J1', 'L new J2,J2,J3', 'P m old',
                'SAVE m ' + replacement, 'U m ' + utf('original.unknown') + ' ' + utf('retained'),
                f'EXP m 1 {interim}', 'SNAP']
    for state_script, force_script in [('I50,I30', 'B1,B1'), ('I30,I50', 'B1,B1'),
                                       ('I60,I70', 'B0,B1'), ('I100,I30', 'B1,B1')]:
        order += setup(50, 1, 1) + ['Q m getState ' + state_script, 'Q m isForceStart ' + force_script, 'EXP m 1 0']
    order += setup(70, 1) + ['PS t 1 2', 'RESUME m', 'SNAP']
    order += setup(70, 0) + ['PS t 0 2', 'RESUME m', 'SNAP']

    faults = groups['getter-fault']
    manager_getters = ['getStats', 'getTorrent', 'getAbsoluteSaveLocation', 'isPersistent', 'getTorrentFileName',
        'getState', 'getErrorType', 'getErrorFlags', 'getErrorDetails', 'getAssumedComplete', 'isForceStart',
        'getPosition', 'getMaxUploads', 'getCreationTime', 'getUserData', 'isDataAlreadyAllocated', 'saveDownload']
    for method in manager_getters:
        for state in [50, 100]:
            faults += setup(state, 1, 1) + ['V m getErrorType I1', f'F m {method} 1 R', 'EXP m 1 0', 'SNAP']
    for field in ['getDownloadRateLimitBytesPerSecond', 'getUploadRateLimitBytesPerSecond',
                  'getTotalDataBytesReceived', 'getTotalDataBytesSent', 'getDownloadCompletedBytes',
                  'getDiscarded', 'getHashFailBytes', 'getSecondsDownloading', 'getSecondsOnlySeeding']:
        faults += setup() + [f'F m:stats {field} 1 R', 'EXP m 1 0', 'SNAP']
    for mode in ['C', 'R']:
        faults += setup() + [f'F t getHash 1 {mode}', 'EXP m 1 0', 'SNAP']
        for operation in ['PAUSE m 0', 'ISPAUSED m', 'RESUME m', 'RESUMING m', 'STOPPAUSED m']:
            faults += setup(70, 1) + ['PS t 1 2', f'F t getHashWrapper 1 {mode}', operation, 'SNAP']
    for operation in ['PAUSE m 0', 'ISPAUSED m', 'RESUME m', 'RESUMING m', 'STOPPAUSED m']:
        for owner, method in [('m', 'getTorrent'), ('HOST', 'getDownloadManager'), ('MON', 'enter'), ('MON', 'exit')]:
            faults += setup(70, 1) + ['PS t 1 2', f'F {owner} {method} 1 R', operation, 'SNAP']
    for method, operation, state in [('stopIt', 'PAUSE m 0', 50), ('stopIt', 'RESUME m', 70),
                                      ('setForceStart', 'RESUME m', 70), ('isPaused', 'PAUSE m 0', 70)]:
        rows = setup(state, 1)
        if operation == 'RESUME m':
            rows += ['PS t ' + ('0' if method == 'stopIt' else '1') + ' 2']
        faults += rows + [f'F m {method} 1 R', operation, 'SNAP']
    faults += setup(70) + ['F TAG stateChanged 1 R', 'PAUSE m 0', 'SNAP']
    faults += setup(70) + ['PS t 1 2', 'F TAG stateChanged 1 R', 'STOPPAUSED m', 'SNAP']

    aliases = groups['alias-policy']
    for values in ['J127,J127', 'J128,J128', 'j127,j127', 'j128,j128', '@shared,@shared',
                   '@shared,@other', '@shared,@other,@shared']:
        aliases += setup() + ['B shared 128', 'B other 128', 'L p ' + values, 'P m p', 'EXP m 1 0', 'OUT']
    aliases += setup() + ['L p J1,J1', 'P m p', 'EXP m 1 0', 'AT t 0 1', 'OUT',
                          'LI p 0 J2', 'OUT', 'EXP m 1 0', 'SNAP']
    aliases += setup(70) + ['M other t 70 0 0 1 1', 'PS t 1 2', 'REG t other', 'RESUMING m',
                            'ISPAUSED m', 'ISPAUSED other', 'STOPPAUSED other', 'SNAP']

    boundaries = groups['provider-boundary']
    for token in ['-', 'N', 'I1,I1', 'u0061', 'J1,N,J1']:
        boundaries += setup() + ['L p ' + token, 'P m p', 'EXP m 1 0', 'SNAP']
    for field in ['getAbsoluteSaveLocation', 'getTorrentFileName', 'getErrorDetails']:
        boundaries += setup(100) + ['V m getErrorType I1', f'V m {field} N', 'EXP m 1 0', 'SNAP']
    for operation in ['ISPAUSED m', 'PAUSE m 0', 'RESUME m', 'RESUMING m', 'STOPPAUSED m']:
        boundaries += setup(70) + ['PS t N', operation, 'SNAP']
    boundaries += ['Z', 'T t N', 'M m t 50 0 0 1 1', 'REG t m', 'EXP m 1 0', 'ISPAUSED m', 'PAUSE m 0', 'SNAP']
    for value in [2**32-1, 2**32, 2**63-1, -2**63]:
        boundaries += setup() + [f'S m getDownloadRateLimitBytesPerSecond {value}', 'EXP m 1 0']
    return groups


def plan():
    output = PLANNED
    source_plan()
    result = {'status': 'planned-only-no-source-or-native-execution', 'groups': {},
        'native_equivalence_assumed': False, 'source_observations': 0,
        'native_policy_future': ['Owned return values and payload/contact handles', 'Entry/queue/effect/resource limits',
            'Structured edit preserving original unknown fields; export itself returns a fresh source map',
            'Actual controller/disk/seeding completion/close and RDResume source seams are separate next work']}
    for name, lines in commands().items():
        data = ('\n'.join(lines) + '\n').encode()
        path = output / (name + '.txt')
        if path.exists() and path.read_bytes() != data:
            raise ValueError(('Do not replace existing preplanned original commands', name))
        if not path.exists():
            raise ValueError(('Missing historical source commands', name))
        result['groups'][name] = {'commands': len(lines), 'sha256': sha(data), 'native_equivalence_assumed': False}
    path = output / 'plan.json'
    encoded = json.dumps(result, indent=2) + '\n'
    if path.exists() and path.read_text() != encoded:
        raise ValueError('Do not replace an existing source plan')
    if not path.exists():
        raise ValueError('Missing historical source plan')
    return result


def freeze(reference):
    # No implicit builder/native execution; preserve preparation failures too.
    development = EVIDENCE / 'development'
    development.mkdir(parents=True, exist_ok=True)
    attempt = pathlib.Path(tempfile.mkdtemp(prefix='phase1-source-run-', dir=str(development)))
    output = FIXTURES
    created_output = False
    source_work = None
    manifest = {'status': 'preparing-original-source-collection', 'reference': reference, 'groups': {},
        'collector_sha256': sha(pathlib.Path(__file__).read_bytes()),
        'native_comparison_performed': False, 'native_equivalence_assumed': False}
    try:
        (attempt / 'status.json').write_text(json.dumps(manifest, indent=2) + '\n')
        pinned_inputs = inputs()
        for name, digest in pinned_inputs.items():
            path = pathlib.Path(name); target = attempt / 'inputs-before-execution' / path.relative_to(ROOT)
            target.parent.mkdir(parents=True, exist_ok=True); shutil.copyfile(path, target)
            if sha(target.read_bytes()) != digest:
                raise ValueError(('Input changed during collection preparation', name))
        candidate = pathlib.Path(reference.get('work', ''))
        if candidate.parent == pathlib.Path('/tmp') and candidate.name.startswith('biglybtc-export-pause-oracle-') and candidate.resolve() == candidate and not candidate.is_symlink():
            source_work = candidate
        verify(reference)
        pinned = plan()
        manifest['planned_manifest_sha256'] = sha((PLANNED / 'plan.json').read_bytes())
        output.mkdir(exist_ok=False)
        created_output = True
        manifest['status'] = 'collecting-original-before-native'
        shutil.copytree(pathlib.Path(reference['work']), attempt / 'reference-work-before-execution')
        shutil.copyfile(PLANNED / 'plan.json', attempt / 'planned.json')
        for name, lines in commands().items():
            data = ('\n'.join(lines) + '\n').encode()
            if sha(data) != pinned['groups'][name]['sha256'] or data != (PLANNED / (name + '.txt')).read_bytes():
                raise ValueError(('Changed preplanned source commands', name))
            verify(reference)
            (output / 'progress.json').write_text(json.dumps({'status': 'running', 'group': name,
                'completed_groups': list(manifest['groups'])}, indent=2) + '\n')
            folder = output / name; folder.mkdir()
            (folder / 'commands.txt').write_bytes(data)
            with (folder / 'source-stdout.txt').open('wb') as stdout, (folder / 'source-stderr.txt').open('wb') as stderr:
                subprocess.run(reference['command'], input=data, stdout=stdout, stderr=stderr, check=True, timeout=180)
            answers = (folder / 'source-stdout.txt').read_bytes()
            if len(answers.splitlines()) != len(lines):
                raise ValueError(('Wrong source observation count', name))
            (folder / 'expected.txt').write_bytes(answers)
            manifest['groups'][name] = {'observations': len(lines),
                'source_exceptions': sum(line.startswith(b'ERR ') for line in answers.splitlines()),
                'files_sha256': files(folder)}
            verify(reference)
            (output / 'manifest-in-progress.json').write_text(json.dumps(manifest, indent=2) + '\n')
        manifest['status'] = 'original-source-frozen-before-native-comparison'
        (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
        return manifest
    except BaseException as error:
        manifest.update(status='failed', failure_type=type(error).__name__, failure=str(error))
        (attempt / 'failure.json').write_text(json.dumps(manifest, indent=2) + '\n')
        if created_output:
            (output / 'failure.json').write_text(json.dumps(manifest, indent=2) + '\n')
        raise
    finally:
        if source_work is not None and source_work.is_dir():
            shutil.copytree(source_work, attempt / 'reference-work-after-execution')
        if created_output:
            shutil.copytree(output, attempt / 'source-output-after-execution')
        (attempt / 'status.json').write_text(json.dumps(manifest, indent=2) + '\n')
        (attempt / 'persistent-files-sha256.json').write_text(json.dumps(files(attempt), indent=2) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--freeze', action='store_true')
    parser.add_argument('--reference', type=pathlib.Path)
    args = parser.parse_args()
    if args.freeze:
        if not args.reference:
            parser.error('--freeze requires a separately root-compiled reference')
        record = freeze(json.loads(args.reference.read_text()))
        print('Frozen original observations', {k: v['observations'] for k, v in record['groups'].items()})
    else:
        record = plan()
        print('Planned only', {k: v['commands'] for k, v in record['groups'].items()})


if __name__ == '__main__':
    main()
