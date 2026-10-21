#!/usr/bin/env python3
"""Replay actual original Java Date-query observations using the native CLI only."""
from pathlib import Path
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require(condition, detail):
    if not condition:
        raise AssertionError(detail)


def integer(text):
    value = int(text)
    require(str(value) == text and -(2**63) <= value < 2**63, 'canonical signed64 field')
    return value


def main():
    require(len(sys.argv) == 3, 'DRIVER_EXECUTABLE FIXTURE_DIRECTORY required')
    driver = Path(sys.argv[1]).resolve(strict=True)
    fixtures = Path(sys.argv[2]).resolve(strict=True)
    require(driver.is_file() and os.access(driver, os.X_OK), 'actual driver executable required')
    provenance = json.loads((fixtures/'provenance.json').read_text())
    require(provenance['schema'] == 1 and provenance['status'] == 'ACTUAL_ORIGINAL_JAVA_DATE_QUERY_OUTPUT', 'actual Java output provenance required')
    require(provenance['original_upstream'] == 'cb8c6cb9bb97916cbc7e80338a017200ffc3e71f', 'exact original revision')
    require(provenance['presence_equality_credit'] == 0 and provenance['original_planner_equality_credit'] == 0, 'scoped numeric comparison')
    inputs = provenance['fixture_sha256']
    def verify_inputs():
        for rel, expected in inputs.items():
            parts = Path(rel)
            require(not parts.is_absolute() and '..' not in parts.parts, 'relative fixture input')
            path = fixtures/parts
            require(path.is_file() and not path.is_symlink() and path.resolve().is_relative_to(fixtures), 'physical fixture file')
            require(digest(path) == expected, 'fixture bytes changed: '+rel)
    verify_inputs()
    plan = json.loads((fixtures/'ranges.json').read_text())
    require(plan['query_count'] == provenance['actual_original_date_queries'], 'actual query count binding')
    require(plan['six_scalar_observations_if_executed'] == provenance['actual_original_scalar_observations'], 'actual scalar count binding')
    expected = {}
    for line in (fixtures/'expected.tsv').read_text(encoding='ascii').splitlines():
        if line.startswith('#'):
            continue
        fields = line.split('\t')
        require(len(fields) == 9 and fields[1] in ('whole', 'bucket'), 'original result row grammar')
        key = fields[0], fields[1], integer(fields[2])
        require(key not in expected, 'duplicate original result')
        expected[key] = tuple(integer(x) for x in fields[3:])
    require(len(expected) == plan['query_count'], 'complete original expected output')
    seen = set()
    comparisons = known = unknown = 0
    deadline = time.monotonic()+90
    with tempfile.TemporaryDirectory(prefix='biglybtc-history-range-replay-') as temporary:
        work = Path(temporary)
        # The original source fixture tree is never passed to the storage driver.
        shutil.copytree(fixtures/'profiles', work/'profiles')
        for directory, _, files in os.walk(work):
            os.chmod(directory, 0o700)
            for name in files:
                os.chmod(Path(directory)/name, 0o600)
        for item in plan['ranges']:
            require(time.monotonic() < deadline, 'bounded replay deadline')
            profile = item['profile']
            require(isinstance(profile, str) and profile in ('normal','spill','extreme','zero','epoch','empty'), 'declared generated profile')
            result = subprocess.run([str(driver), str(work/'profiles'/profile), item['period'], str(item['offset']), str(item['now_millis'])],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=min(10, deadline-time.monotonic()), check=False)
            require(result.returncode == 0, 'actual driver failed '+item['id']+': '+result.stderr.decode('utf-8', errors='replace'))
            lines = [line.split('\t') for line in result.stdout.decode('ascii').splitlines()]
            require(len(lines) == item['bucket_count']+3, 'complete native result rows')
            require(lines[0] == ['plan']+list(map(str, [item['start_millis'],item['end_millis'],item['bucket_millis'],item['bucket_count']])), 'declared plan shape')
            require(len(lines[-1]) == 3 and lines[-1][0] == 'coverage' and all(integer(v) >= 0 for v in lines[-1][1:]), 'coverage receipt')
            for n, fields in enumerate(lines[1:-1]):
                if n == 0:
                    require(len(fields) == 9 and fields[0] == 'whole', 'whole result grammar')
                    key = item['id'], 'whole', -1
                    state, observations, values = fields[1], fields[2], fields[3:]
                else:
                    index = n-1
                    start = item['start_millis']+index*item['bucket_millis']
                    require(len(fields) == 12 and fields[:4] == ['bucket',str(index),str(start),str(start+item['bucket_millis'])], 'bucket identity/interval')
                    key = item['id'], 'bucket', index
                    state, observations, values = fields[4], fields[5], fields[6:]
                require(key not in seen and key in expected, 'exact query membership')
                seen.add(key)
                require(state in ('0','1') and integer(observations) >= 0, 'native optional state')
                if state == '0':
                    require(integer(observations) == 0 and values == ['-']*6, 'unknown native result')
                    # Original Date-query API returns six numeric zeros when no
                    # record was selected. This earns no presence equivalence.
                    actual = (0,)*6
                    unknown += 1
                else:
                    require(integer(observations) > 0, 'observed known native result')
                    actual = tuple(integer(value) for value in values)
                    known += 1
                require(actual == expected[key], 'original scalar mismatch '+str(key)+': '+str(actual)+' != '+str(expected[key]))
                comparisons += 6
        # Driver may use its ordinary lock file in the disposable profile root;
        # every supplied raw history file must remain byte-identical.
        for rel, expected_hash in inputs.items():
            if rel.startswith('profiles/'):
                require(digest(work/rel) == expected_hash, 'generated raw history changed: '+rel)
    verify_inputs()
    require(seen == set(expected) and comparisons == provenance['actual_original_scalar_observations'], 'complete scalar replay')
    print('PASS '+str(plan['range_count'])+' saved-history-range corpus ranges; '+str(len(seen))+' original-Java Date queries; '+str(comparisons)+' scalar comparisons; no presence/planner/UI equality credit')
    print('native_known='+str(known)+' native_unknown_numeric_zero='+str(unknown))


if __name__ == '__main__':
    main()
