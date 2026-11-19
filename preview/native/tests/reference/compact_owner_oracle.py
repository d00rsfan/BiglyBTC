#!/usr/bin/env python3
"""Actual original constructors,priority receiver and Compact requirements.

Requires separately attested cb8c6cb9bb97916cbc7e80338a017200ffc3e71f
classes. All JVM configuration,cache controls and generated files stay in output.
"""
from pathlib import Path
import argparse, hashlib, json, re, subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--original-classes', type=Path, required=True)
p.add_argument('--java-home', type=Path, required=True)
p.add_argument('--library', type=Path, action='append', default=[])
p.add_argument('--output', type=Path, required=True)
a = p.parse_args(); output = a.output.resolve(); output.mkdir(parents=True, exist_ok=True)
classes = output/'classes'; classes.mkdir(exist_ok=True)
profile = output/'profile'; profile.mkdir(exist_ok=True)
files = output/'owned-files'; files.mkdir(exist_ok=True)
original = a.original_classes.resolve(strict=True)
source = Path(__file__).with_name('CompactOwnerOracle.java')
priority = Path(__file__).with_name('FilePriorityLoadOracle.java')
cp = ':'.join(map(str, [classes, original, *a.library]))
rows = []
for count in range(1, 7):
    for base in [[0, 16384, 1], [20000, 12768, 0], [16383, 0, 16385]]:
        lengths = [base[i % len(base)] for i in range(count)]
        if not any(lengths): lengths[-1] = 1
        for kinds in [[kind]*count for kind in range(1, 5)] + [[2, 1, 4, 3, 2, 4][:count]]:
            for padding in [0, 1 << (count // 2)]:
                policies = ['absent', 'empty', ','.join(['-1']*count), ','.join(['4294967295']*count),
                    ','.join(str([0, 1, -4][i % 3]) for i in range(count)), ','.join(['0']*count),
                    ','.join(['1']*count), '1']
                for policy in policies:
                    for mode in [0, 1, 2]:
                        rows.append('\t'.join(map(str, [len(rows), ','.join(map(str, lengths)), ','.join(map(str, kinds)), policy, padding, mode])))
inputs = output/'cases.tsv'; inputs.write_text('\n'.join(rows) + '\n')
subprocess.run([str(a.java_home/'bin/javac'), '-cp', cp, '-d', str(classes), str(priority), str(source)], check=True)
raw = output/'original-stdout.log'; errors = output/'original-stderr.log'
with inputs.open('rb') as i, raw.open('wb') as o, errors.open('wb') as e:
    subprocess.run([str(a.java_home/'bin/java'), '-Xmx512m', '-Dazureus.config.path='+str(profile),
        '-Dazureus.install.path='+str(output), '-Doracle.expected.classes='+str(original),
        '-cp', cp, 'CompactOwnerOracle', str(files)], stdin=i, stdout=o, stderr=e, check=True, timeout=240)
normalized = [line for line in raw.read_text().splitlines() if re.match(r'^\d+\t', line)]
assert len(normalized) == len(rows) and all(line.split('\t')[0] == str(i) for i, line in enumerate(normalized))
result = output/'original.tsv'; result.write_text('\n'.join(normalized)+'\n')
match = re.search(r'PASS_ORIGINAL_COMPACT_OWNER (\d+) rows; (\d+) actual constructors; (\d+) actual predicates', errors.read_text())
assert match and int(match[1]) == len(rows)
receipt = dict(baseline='cb8c6cb9bb97916cbc7e80338a017200ffc3e71f', rows=len(rows),
    actual_constructors=int(match[2]), actual_predicates=int(match[3]),
    inputs_sha256=hashlib.sha256(inputs.read_bytes()).hexdigest(), output_sha256=hashlib.sha256(result.read_bytes()).hexdigest(),
    helper_sha256=hashlib.sha256(source.read_bytes()).hexdigest(), priority_helper_sha256=hashlib.sha256(priority.read_bytes()).hexdigest())
for name in ['com/biglybt/core/disk/impl/DiskManagerFileInfoImpl.class', 'com/biglybt/core/disk/impl/DiskManagerFileInfoSetImpl.class', 'com/biglybt/core/disk/impl/DiskManagerUtil.class', 'com/biglybt/core/disk/impl/resume/RDResumeHandler.class']:
    receipt[name] = hashlib.sha256((original/name).read_bytes()).hexdigest()
(output/'receipt.json').write_text(json.dumps(receipt, indent=2)+'\n')
print('PASS_ORIGINAL_COMPACT_OWNER_REFERENCE',len(rows),receipt['actual_constructors'],receipt['actual_predicates'])
