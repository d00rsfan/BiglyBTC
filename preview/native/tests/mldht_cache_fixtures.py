"""Replay original routing-entry codec values and preserve complete cache bytes."""
import hashlib
import json
import pathlib
import shutil
import subprocess
import sys
import tempfile
root=pathlib.Path(__file__).resolve().parents[2]/'migration/fixtures/mldht-cache'
manifest=json.loads((root/'manifest.json').read_text())
work=pathlib.Path(tempfile.mkdtemp(prefix='biglybtc-mldht-cache-fixtures-'))
accepted=rejected=0
for case in manifest['cases']:
    data=(root/case['input']).read_bytes();expected=(root/case['expected']).read_bytes()
    assert hashlib.sha256(data).hexdigest()==case['input_sha256']
    assert hashlib.sha256(expected).hexdigest()==case['expected_sha256']
    source=work/case['input'];source.write_bytes(data);inode=source.stat().st_ino
    destination=source.with_suffix('.native')
    result=subprocess.run([sys.argv[1],str(source),str(destination)],check=True,capture_output=True,timeout=15)
    assert result.stdout==expected,case['input']
    assert source.read_bytes()==data and source.stat().st_ino==inode
    if expected==b'REJECT\n':
        assert not destination.exists();rejected+=1
    else:
        assert destination.read_bytes()==data;accepted+=1
print('PASS original routing cache:',accepted,'decoded with identical bytes,',rejected,'rejected without writes')
