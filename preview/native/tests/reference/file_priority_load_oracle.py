#!/usr/bin/env python3
"""Qualify actual original DiskManagerUtil priority restore/cleanup calls.

Requires independently attested cb8c6cb9bb97916cbc7e80338a017200ffc3e71f
classes and JDK17. All reference JVM configuration/output is isolated beneath
--output. This helper is not linked or invoked by the native application.
"""
from pathlib import Path
import argparse, hashlib, json, random, subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--original-classes', type=Path, required=True)
p.add_argument('--java-home', type=Path, required=True)
p.add_argument('--library', type=Path, action='append', default=[])
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
output = a.output.resolve()
output.mkdir(parents=True, exist_ok=True)
classes = output/'classes'
classes.mkdir(exist_ok=True)
profile = output/'profile'
profile.mkdir(exist_ok=True)
original = a.original_classes.resolve(strict=True)
source = Path(__file__).with_name('FilePriorityLoadOracle.java')
cp = ':'.join(map(str, [classes, original, *a.library]))
rng = random.Random(2026111801)
numbers = [-9223372036854775808, -4294967297, -4294967296,
           -2147483648, -4, -1, 0, 1, 9, 2147483647,
           2147483648, 4294967295, 4294967296, 4294967297,
           9223372036854775807]
rows = []
def add(count, values):
    rows.append(f'{len(rows)}\t{count}\t{values}')
for count in [0, 1, 2, 3, 4, 8, 16, 32]:
    add(count, 'absent')
    add(count, 'empty')
    for value in numbers:
        add(count, ','.join([str(value)] * count) or 'empty')
    for generation in range(180):
        add(count, ','.join(str(rng.choice(numbers)) for _ in range(count)) or 'empty')
    # The original inspects only file-count elements, including narrowing -1.
    for extra in ['0', 'null', 'bad', '4294967296']:
        add(count, ','.join(['-1'] * count + [extra]))
    if count:
        for prefix in ['-1', '0', '2147483648', 'null', 'bad']:
            add(count, prefix)
            add(count, ','.join([prefix] + ['-1'] * (count - 1)))
inputs = output/'cases.tsv'
inputs.write_text('\n'.join(rows) + '\n')
subprocess.run([str(a.java_home/'bin/javac'), '-cp', cp, '-d', str(classes), str(source)], check=True)
result = output/'original.tsv'
diagnostics = output/'original-stderr.log'
with inputs.open('rb') as i, result.open('wb') as o, diagnostics.open('wb') as e:
    subprocess.run([str(a.java_home/'bin/java'), '-Xmx256m',
        '-Dazureus.config.path=' + str(profile), '-Dazureus.install.path=' + str(output),
        '-Doracle.expected.classes=' + str(original), '-cp', cp, 'FilePriorityLoadOracle'],
        stdin=i, stdout=o, stderr=e, check=True, timeout=120)
stderr = diagnostics.read_text()
assert f'PASS_ORIGINAL_FILE_PRIORITY_LOAD {len(rows)} ' in stderr
actual = result.read_text().splitlines()
assert len(actual) == len(rows) and all(line.split('\t')[0] == str(index) for index, line in enumerate(actual))
receipt = dict(baseline='cb8c6cb9bb97916cbc7e80338a017200ffc3e71f', rows=len(rows),
    inputs_sha256=hashlib.sha256(inputs.read_bytes()).hexdigest(),
    output_sha256=hashlib.sha256(result.read_bytes()).hexdigest(),
    helper_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
    retained_generated_diagnostics_bytes=diagnostics.stat().st_size)
for name in ['com/biglybt/core/disk/impl/DiskManagerUtil.class', 'com/biglybt/core/disk/impl/DiskManagerFileInfoImpl.class']:
    receipt[name] = hashlib.sha256((original/name).read_bytes()).hexdigest()
(output/'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
print('PASS_ORIGINAL_FILE_PRIORITY_LOAD_REFERENCE', len(rows))
