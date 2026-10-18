"""Replay original persisted-plugin prefixes, defaults and numeric conversion."""
import hashlib
import json
import pathlib
import subprocess
import sys
import tempfile
root=pathlib.Path(__file__).resolve().parents[2]/'migration/fixtures/plugin-settings'
manifest=json.loads((root/'manifest.json').read_text())
work=pathlib.Path(tempfile.mkdtemp(prefix='biglybtc-plugin-settings-'))
for case in manifest['cases']:
    for label in ['input','folder','expected']:
        assert hashlib.sha256((root/case[label]).read_bytes()).hexdigest()==case[label+'_sha256']
    data=(root/case['input']).read_bytes();path=work/case['input'];path.write_bytes(data);inode=path.stat().st_ino
    folder=(root/case['folder']).read_text()
    result=subprocess.run([sys.argv[1],str(path),folder],check=True,capture_output=True,timeout=10)
    assert result.stdout==(root/case['expected']).read_bytes(),case['input']
    assert path.read_bytes()==data and path.stat().st_ino==inode
for invalid in ['', '.', '..', 'bad/name']:
    result=subprocess.run([sys.argv[1],str(path),invalid],check=True,capture_output=True,timeout=10)
    assert result.stdout==b'REJECT\n'
print('PASS original plugin settings:',len(manifest['cases']),'snapshots; unchanged files and rejected invalid directory bindings')
