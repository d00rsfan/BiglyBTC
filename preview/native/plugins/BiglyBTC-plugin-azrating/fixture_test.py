"""Replay immutable original observations; preserve failing outputs for diagnosis."""
import hashlib
import json
import pathlib
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parent / 'fixtures'
if not root.exists():
    root = pathlib.Path(__file__).resolve().parents[3] / 'migration/fixtures/rating-data'
manifest = json.loads((root / 'manifest.json').read_text())
commands = (root / 'commands.txt').read_bytes()
expected = (root / 'expected.txt').read_bytes()
assert hashlib.sha256(commands).hexdigest() == manifest['commands_sha256']
assert hashlib.sha256(expected).hexdigest() == manifest['expected_sha256']
work = pathlib.Path(tempfile.mkdtemp(prefix='biglybtc-rating-replay-'))
path = work / 'commands.txt'
path.write_bytes(commands)
inode = path.stat().st_ino
result = subprocess.run([sys.argv[1], str(path)], check=True, capture_output=True, timeout=60)
(work / 'native.txt').write_bytes(result.stdout)
(work / 'stderr.txt').write_bytes(result.stderr)
actual = result.stdout.splitlines()
original = expected.splitlines()
assert len(actual) == len(original), (work, len(actual), len(original))
for index, (a, b) in enumerate(zip(actual, original)):
    assert a == b, (work, index, commands.splitlines()[index], a, b)
assert path.read_bytes() == commands and path.stat().st_ino == inode
print('PASS original rating data:', manifest['observations'], 'observations; read-only inputs;', work)
