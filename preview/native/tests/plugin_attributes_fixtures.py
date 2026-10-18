"""Replay original cached/primary map and full64 configuration observations."""
import hashlib
import json
import pathlib
import subprocess
import sys
import tempfile
root = pathlib.Path(__file__).resolve().parents[2] / 'migration/fixtures/plugin-attributes'
manifest = json.loads((root / 'manifest.json').read_text())
commands, expected = (root / 'commands.txt').read_bytes(), (root / 'expected.txt').read_bytes()
assert hashlib.sha256(commands).hexdigest() == manifest['commands_sha256']
assert hashlib.sha256(expected).hexdigest() == manifest['expected_sha256']
work = pathlib.Path(tempfile.mkdtemp(prefix='biglybtc-plugin-attributes-replay-'))
path = work / 'commands.txt'; path.write_bytes(commands); inode = path.stat().st_ino
result = subprocess.run([sys.argv[1], str(path)], check=True, capture_output=True, timeout=60)
(work / 'native.txt').write_bytes(result.stdout); (work / 'stderr.txt').write_bytes(result.stderr)
actual, original = result.stdout.splitlines(), expected.splitlines()
assert len(actual) == len(original)
for i, (a, b) in enumerate(zip(actual, original)):
    assert a == b, (work, i, a, b)
assert path.read_bytes() == commands and path.stat().st_ino == inode
print('PASS original plugin attributes/full64:', manifest['observations'], 'observations;', work)
