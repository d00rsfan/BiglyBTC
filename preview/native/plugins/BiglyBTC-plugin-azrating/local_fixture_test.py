"""Immutable original local-rating observations, including all UTF-16 units."""
import hashlib
import json
import pathlib
import subprocess
import sys
import tempfile

root=pathlib.Path(__file__).resolve().parent/'local-fixtures'
if not root.exists(): root=pathlib.Path(__file__).resolve().parents[3]/'migration/fixtures/rating-local'
manifest=json.loads((root/'manifest.json').read_text())
for name,digest in manifest['files_sha256'].items():
    assert hashlib.sha256((root/name).read_bytes()).hexdigest()==digest
work=pathlib.Path(tempfile.mkdtemp(prefix='biglybtc-rating-local-replay-'))
commands=(root/'commands.txt').read_bytes();expected=(root/'expected.txt').read_bytes()
path=work/'commands.txt';path.write_bytes(commands);inode=path.stat().st_ino
result=subprocess.run([sys.argv[1],str(path)],capture_output=True,check=True,timeout=90)
(work/'native.txt').write_bytes(result.stdout);(work/'stderr.txt').write_bytes(result.stderr)
assert len(result.stdout.splitlines())==manifest['observations']
if result.stdout!=expected:
    for i,(native,java) in enumerate(zip(result.stdout.splitlines(),expected.splitlines())):
        assert native==java,(work,i,commands.splitlines()[i],native,java)
assert path.read_bytes()==commands and path.stat().st_ino==inode
print('PASS original local rating policy:',manifest['observations'],'including',manifest['exhaustive_bmp_cases'],'UTF16 units;',work)
