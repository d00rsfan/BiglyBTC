#!/usr/bin/env python3
"""Plan completion source cases only; freeze requires a root-built reference."""
import argparse
import itertools
import json
import pathlib
import shutil
import subprocess
import tempfile

from session_completion_oracle import (ROOT, EVIDENCE, PLANNED, FIXTURES, files, inputs, sha, source_plan, verify)


def setup(retain=0, check=1, before=0, force=1, remaining=0, full=0, result='DONE'):
    return ['Z', f'CFG {retain} {check} {before}', 'CLOCK M:101,W:202,M:303', f'FORCE {force}',
        f'DISK {remaining} {full} {result}', 'INIT 0 123 1 17 -1 -1 4 OLD', 'PEER A 1', 'PEER B 0']


def cases():
    groups = {name: [] for name in ['controller-semantic', 'peer-semantic', 'clock-order',
                                   'partial-fault', 'boundary-contract']}

    def add(group, name, prefix, operations, errors=(), caught=()):
        groups[group].append({'id': name, 'commands': prefix + operations,
            'expected_errors': {str(len(prefix) + index): error for index, error in errors},
            'expected_caught_faults': {str(len(prefix) + index): count for index, count in caught}})

    for never, retain, forced in itertools.product([0, 1], repeat=3):
        add('controller-semantic', f'never{never}-retain{retain}-force{forced}',
            setup(retain=retain, force=forced), [f'SEED {never}', 'SNAP'])

    for start, retain, forced, check, before, full, result in itertools.product(
            [0, 1], [0, 1], [0, 1], [0, 1], [0, 1], [0, 99], ['N', 'DONE']):
        add('peer-semantic', f'start{start}-retain{retain}-force{forced}-check{check}-before{before}-full{full}-result{result}',
            setup(retain, check, before, forced, full=full, result=result), [f'CHECK {start}', 'SNAP'])
    for start, remaining in itertools.product([0, 1], [1, 99, 2**63-1]):
        add('peer-semantic', f'incomplete-start{start}-remaining{remaining}',
            setup(remaining=remaining, full=remaining), [f'CHECK {start}', 'SNAP'])
    for start in [0, 1]:
        for count in [0, 1, 3]:
            prefix = setup()
            prefix = [line for line in prefix if not line.startswith('PEER ')]
            prefix += [f'PEER P{i} {i % 2}' for i in range(count)]
            add('peer-semantic', f'peer-count{count}-start{start}', prefix, [f'CHECK {start}', 'SNAP'])
    add('peer-semantic', 'complete-then-incomplete-kernel-leaves-other-fields', setup(),
        ['CHECK 0', 'DISK 1 99 NEW', 'CHECK 0', 'SNAP'])

    clocks = [(-2**63, -2**63, -2**63), (-1, 0, 1), (0, 0, 0), (1, -1, 0),
        (2**63-1, 2**63-1, 2**63-1), (2**63-1, -2**63, -1), (101, 303, 202)]
    for start in [0, 1]:
        for first_mono, wall, second_mono in clocks:
            add('clock-order', f'start{start}-M{first_mono}-W{wall}-M{second_mono}', setup(),
                [f'CLOCK M:{first_mono},W:{wall},M:{second_mono}', f'CHECK {start}', 'SNAP'])
    add('clock-order', 'incomplete-consumes-no-clock', setup(remaining=1), ['CLOCK -', 'CHECK 0', 'SNAP'])
    add('clock-order', 'controller-consumes-no-clock', setup(), ['CLOCK -', 'SEED 0', 'SNAP'])

    faults = [('REMAINING_EX_DND', 1), ('CLEAR_ENDGAME', 1), ('ADAPTER_FINISH', 1),
        ('CTRL_FINISH', 1), ('CTRL_FINISH', 2), ('TIME', 1), ('TIME', 2), ('TIME', 3),
        ('PEER_SNUB', 1), ('PEER_SNUB', 2), ('ADAPTER_SEED', 1), ('DM_ENDED', 1),
        ('CTRL_STATE', 1), ('CTRL_FORCE', 1), ('CONFIG', 1), ('CONFIG', 2), ('CONFIG', 3),
        ('DISK_ENDED', 1), ('CREATE_CHECK', 1), ('ENQUEUE_CHECK', 1)]
    for before in [0, 1]:
        for point, occurrence in faults:
            add('partial-fault', f'before{before}-{point}-{occurrence}', setup(before=before),
                [f'FAULT {point} {occurrence}', 'CHECK 0', 'SNAP'], errors=[(1, 'SuppliedFailure')])
    for before in [0, 1]:
        add('partial-fault', f'caught-save-resume-before{before}', setup(before=before),
            ['FAULT SAVE_RESUME 1', 'CHECK 0', 'SNAP'], caught=[(1, 1)])
    add('partial-fault', 'debug-failure-after-caught-resume-save', setup(),
        ['FAULT SAVE_RESUME 1', 'FAULT CAUGHT 1', 'CHECK 0', 'SNAP'],
        errors=[(2, 'SuppliedFailure')], caught=[(2, 1)])
    for point in ['CTRL_FINISH', 'DM_ENDED', 'CTRL_STATE', 'CTRL_FORCE', 'CONFIG']:
        add('partial-fault', 'controller-' + point, setup(),
            [f'FAULT {point} 1', 'SEED 0', 'SNAP'], errors=[(1, 'SuppliedFailure')])

    for start in [0, 1]:
        add('boundary-contract', f'supplied-nonforwarding-adapter-start{start}', setup(),
            ['FORWARD 0', f'CHECK {start}', 'SNAP'])
        add('boundary-contract', f'supplied-null-check-request-start{start}', setup(),
            ['NULLREQ 1', f'CHECK {start}', 'SNAP'])
    for remaining in [-1, -2**63]:
        add('boundary-contract', 'supplied-negative-remaining-' + str(remaining), setup(remaining=remaining), ['CHECK 0', 'SNAP'])
    add('boundary-contract', 'repeated-check-kernel-not-caller-state-machine', setup(),
        ['CLOCK M:1,W:2,M:3,M:4,W:5,M:6', 'CHECK 0', 'CHECK 1', 'SNAP'])
    add('boundary-contract', 'repeated-controller-recording-force-intentions', setup(),
        ['SEED 0', 'SEED 0', 'SNAP'])
    return groups


def plan():
    output = PLANNED
    source_plan()
    record = {'status': 'planned-only-no-source-or-native-execution', 'groups': {},
        'source_observations': 0, 'native_equivalence_assumed': False,
        'native_policy_future': ['Owned state/event/lifetime and resource caps',
            'Native failure/flush/publish barrier; caught source resume-save failure does not authorize unsafe completion',
            'Actual controller/disk/peer effects and DND/full tracker policy remain separate',
            'RDResume and conservative recheck checkpoint policy remain pending']}
    for name, scenarios in cases().items():
        lines = [line for scenario in scenarios for line in scenario['commands']]
        data = ('\n'.join(lines) + '\n').encode()
        path = output / (name + '.txt')
        if path.exists() and path.read_bytes() != data:
            raise ValueError(('Preserve pre-execution source commands', name))
        if not path.exists():
            raise ValueError(('Missing historical source commands', name))
        record['groups'][name] = {'scenarios': len(scenarios), 'commands': len(lines), 'sha256': sha(data),
            'contracts': [{key: scenario[key] for key in ['id', 'expected_errors', 'expected_caught_faults']} for scenario in scenarios],
            'native_equivalence_assumed': False}
    record['total_scenarios'] = sum(x['scenarios'] for x in record['groups'].values())
    record['total_commands'] = sum(x['commands'] for x in record['groups'].values())
    encoded = json.dumps(record, indent=2) + '\n'
    path = output / 'plan.json'
    if path.exists() and path.read_text() != encoded:
        raise ValueError('Preserve the original source plan')
    if not path.exists():
        raise ValueError('Missing historical source plan')
    return record


def validate_errors(scenarios, answers):
    offset = 0
    for scenario in scenarios:
        errors, caught = {}, {}
        for index, line in enumerate(answers[offset:offset + len(scenario['commands'])]):
            text = line.decode('ascii')
            if text.startswith('ERR '):
                errors[str(index)] = text.split(' ', 2)[1]
            trace = text.split(' TRACE=', 1)[1]
            count = trace.split(';').count('CAUGHT:SuppliedFailure')
            if count:
                caught[str(index)] = count
            if 'ScriptFailure' in text:
                raise ValueError(('Accidental supplied script failure', scenario['id'], index))
        if errors != scenario['expected_errors'] or caught != scenario['expected_caught_faults']:
            raise ValueError(('Unexpected source failure contract', scenario['id'], errors, caught))
        offset += len(scenario['commands'])
    if offset != len(answers):
        raise ValueError('Unexpected output count')


def freeze(reference):
    # No implicit builder/native execution; preserve preparation failures too.
    development = EVIDENCE / 'development'
    development.mkdir(parents=True, exist_ok=True)
    attempt = pathlib.Path(tempfile.mkdtemp(prefix='phase2-source-run-', dir=str(development)))
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
        if candidate.parent == pathlib.Path('/tmp') and candidate.name.startswith('biglybtc-completion-oracle-') and candidate.resolve() == candidate and not candidate.is_symlink():
            source_work = candidate
        verify(reference)
        pinned = plan()
        manifest['plan_sha256'] = sha((PLANNED / 'plan.json').read_bytes())
        output.mkdir(exist_ok=False)
        created_output = True
        manifest['status'] = 'collecting-original-before-native'
        shutil.copytree(pathlib.Path(reference['work']), attempt / 'reference-work-before-execution')
        shutil.copyfile(PLANNED / 'plan.json', attempt / 'planned.json')
        for name, scenarios in cases().items():
            data = ('\n'.join(line for scenario in scenarios for line in scenario['commands']) + '\n').encode()
            if sha(data) != pinned['groups'][name]['sha256'] or data != (PLANNED / (name + '.txt')).read_bytes():
                raise ValueError(('Changed preplanned source commands', name))
            verify(reference)
            (output / 'progress.json').write_text(json.dumps({'status': 'running', 'group': name,
                'completed_groups': list(manifest['groups'])}, indent=2) + '\n')
            folder = output / name; folder.mkdir(); (folder / 'commands.txt').write_bytes(data)
            with (folder / 'source-stdout.txt').open('wb') as stdout, (folder / 'source-stderr.txt').open('wb') as stderr:
                subprocess.run(reference['command'], input=data, stdout=stdout, stderr=stderr, check=True, timeout=180)
            answers = (folder / 'source-stdout.txt').read_bytes()
            if len(answers.splitlines()) != pinned['groups'][name]['commands']:
                raise ValueError(('Unexpected source output count', name))
            validate_errors(scenarios, answers.splitlines())
            (folder / 'expected.txt').write_bytes(answers)
            manifest['groups'][name] = {'observations': len(answers.splitlines()), 'scenarios': len(scenarios), 'files_sha256': files(folder)}
            verify(reference)
            (output / 'manifest-in-progress.json').write_text(json.dumps(manifest, indent=2) + '\n')
        manifest['status'] = 'source-frozen-before-native-comparison'
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
        result = freeze(json.loads(args.reference.read_text()))
        print('Frozen original source', {k: v['observations'] for k, v in result['groups'].items()})
    else:
        result = plan()
        print('Planned only', result['total_scenarios'], result['total_commands'])


if __name__ == '__main__':
    main()
