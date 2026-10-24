#!/usr/bin/env python3
"""Compare only frozen admitted scalar keys; never calculate expected ranks."""
import argparse
import hashlib
import json
from pathlib import Path


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def observations(path, allowed_ids, marker):
    values = {}
    terminal = []
    for line in path.read_text().splitlines():
        fields = line.split('\t')
        if fields[0] == 'OBS':
            if len(fields) != 4 or fields[1] not in allowed_ids or fields[2] not in ('seedingRank', 'firstPriority'):
                raise ValueError('Unexpected observation: ' + line)
            key = (fields[1], fields[2])
            if key in values or str(int(fields[3])) != fields[3]:
                raise ValueError('Duplicate/noncanonical observation: ' + line)
            values[key] = int(fields[3])
        elif fields[0] == 'TRACE':
            if len(fields) != 3 or fields[1] not in allowed_ids:
                raise ValueError('Unexpected provider trace')
        elif fields[0] in ('REJECT', 'UNKNOWN'):
            if len(fields) < 3 or fields[1] not in allowed_ids:
                raise ValueError('Unexpected native boundary')
        elif fields[0] in ('PASS_QUEUE_RANK_ORIGINAL', 'PASS_QUEUE_RANK_NATIVE'):
            terminal.append(fields)
        else:
            raise ValueError('Unexpected stdout: ' + line)
    if len(terminal) != 1 or terminal[0][0] != marker or int(terminal[0][1]) != len(allowed_ids):
        raise ValueError('Missing/incorrect actual terminal marker')
    if marker == 'PASS_QUEUE_RANK_ORIGINAL' and (len(terminal[0]) != 3 or int(terminal[0][2]) != len(allowed_ids) * 2):
        raise ValueError('Incorrect original scalar count')
    if marker == 'PASS_QUEUE_RANK_NATIVE' and len(terminal[0]) != 2:
        raise ValueError('Incorrect native marker')
    return values


def compare(vectors, original, native):
    cases = json.loads(vectors.read_text())['cases']
    ids = {case['id'] for case in cases}
    if len(ids) != len(cases):
        raise ValueError('Duplicate frozen case IDs')
    expected = observations(original, ids, 'PASS_QUEUE_RANK_ORIGINAL')
    actual = observations(native, ids, 'PASS_QUEUE_RANK_NATIVE')
    if len(expected) != len(cases) * 2:
        raise ValueError('Original did not observe every scalar')
    mismatches = []
    comparisons = 0
    for case in cases:
        for field in case['compare_fields']:
            key = (case['id'], field)
            comparisons += 1
            if key not in actual or actual[key] != expected[key]:
                mismatches.append({'case': key[0], 'field': key[1], 'original': expected[key], 'native': actual.get(key)})
    return {'status': 'PASS' if not mismatches else 'FAIL', 'original_cases': len(cases),
        'original_observed_scalars': len(expected), 'admitted_scalar_comparisons': comparisons,
        'excluded_cases': [case['id'] for case in cases if not case['compare_fields']],
        'mismatches': mismatches, 'vectors_sha256': digest(vectors),
        'original_stdout_sha256': digest(original), 'native_stdout_sha256': digest(native),
        'scope': 'Actual retained Java rank/FP methods under explicit inert providers versus pure native scalar outputs. No whole scheduler, timers, ordering effects, engine activation or client parity.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--vectors', type=Path, required=True)
    parser.add_argument('--original', type=Path, required=True)
    parser.add_argument('--native', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    assert not args.output.exists()
    result = compare(args.vectors, args.original, args.native)
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    if result['status'] != 'PASS':
        raise SystemExit(1)


if __name__ == '__main__':
    main()
