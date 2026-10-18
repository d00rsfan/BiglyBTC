#!/usr/bin/env python3
"""Replay generated metadata snapshots; verify every byte, inode and symlink."""
import base64
import hashlib
import json
import pathlib
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]

def fingerprint(root):
    result = {}
    for path in sorted(root.rglob('*')):
        stat = path.lstat()
        content = ('link', str(path.readlink())) if path.is_symlink() else (
            ('file', hashlib.sha256(path.read_bytes()).hexdigest()) if path.is_file() else ('dir',))
        result[str(path.relative_to(root))] = (stat.st_dev, stat.st_ino, stat.st_mode, content)
    return result

def main():
    directory = ROOT / 'migration/fixtures/rpc-profile'
    manifest = json.loads((directory / 'manifest.json').read_text())
    raw = (directory / 'cases.json').read_bytes()
    assert hashlib.sha256(raw).hexdigest() == manifest['cases_sha256']
    cases = json.loads(raw)
    assert len(cases) == manifest['cases']
    with tempfile.TemporaryDirectory(prefix='biglybtc-rpc-profile-') as temp:
        work = pathlib.Path(temp)
        for case in cases:
            profile = work / case['name']; profile.mkdir()
            # Preexisting lock inode must also survive both reads and cancellation.
            (profile / '.azlock').touch()
            for name, encoded in case['files'].items():
                target = profile / name
                assert not pathlib.Path(name).is_absolute() and '..' not in pathlib.Path(name).parts
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(base64.b64decode(encoded, validate=True))
            for name, target in case.get('links', {}).items():
                assert '..' not in pathlib.Path(target).parts and not pathlib.Path(target).is_absolute()
                (profile / name).symlink_to(target)
            # A saved external torrent/payload path must never be opened. The
            # primary-cache-only cases also use FIFOs; an accidental read blocks.
            for name in case.get('fifos', []):
                import os
                os.mkfifo(profile / name)
            before = fingerprint(profile)
            result = subprocess.run([sys.argv[1], str(profile), case['folder']],
                check=True, capture_output=True, text=True, timeout=15)
            expected = case['expected'] + case['native_details']
            assert result.stdout == expected, (case['name'], result.stdout, expected)
            assert fingerprint(profile) == before, 'Read changed profile: ' + case['name']
            shutil.rmtree(profile)
        # Native safety/recovery policy is checked separately from Java getter
        # parity. These inputs do not start a Java application or restore stubs.
        profile = work / 'native-boundaries'; (profile / 'active').mkdir(parents=True)
        (profile / '.azlock').touch()
        (profile / 'downloads.config').write_bytes(b'd9:downloadslee')
        def run(expected, code=0):
            before = fingerprint(profile)
            result = subprocess.run([sys.argv[1], str(profile), 'xmwebui'], capture_output=True,text=True,timeout=30)
            assert result.returncode == code, result.stderr
            assert (result.stdout if code == 0 else result.stderr) == expected
            assert fingerprint(profile) == before
        (profile / 'biglybt.config').write_bytes(b'broken')
        (profile / 'biglybt.config.bak').write_bytes(b'd27:Plugin.xmwebui.xmui.next.idi29ee')
        run('COUNTER 29 0\nDETAIL 0 0 0 0 0 0\n')
        (profile / 'biglybt.config').unlink()
        (profile / 'biglybt.config.bak').unlink()
        (profile / 'protected').write_bytes(b'protected')
        (profile / 'biglybt.config').symlink_to('protected')
        run('COUNTER - 0\nDETAIL 0 0 0 0 0 1\n')
        (profile / 'biglybt.config').unlink()
        (profile / 'dlarchive.config').symlink_to('protected')
        run('COUNTER 1 0\nDETAIL 0 0 0 0 0 1\n')
        (profile / 'dlarchive.config').unlink()
        stub = b'd4:attrde4:hash20:' + b'a'*20 + b'e'
        (profile / 'dlarchive.config').write_bytes(b'd5:stubsl'+stub*100001+b'ee')
        run('Original RPC identity snapshot exceeds its torrent limit\n',4)
    print('PASS original getter profile fixtures:', len(cases), 'twice per held lease, cancellation, bytes/inodes/links preserved')
    print('PASS native config backup, unsafe config/archive paths and combined identity limit; all bytes/inodes preserved')

if __name__ == '__main__': main()
