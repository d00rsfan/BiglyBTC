#!/usr/bin/env python3
"""One-pass Java cache hydration retains resume authority across stop/crash/conflict."""
import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time


def encode(value):
    if isinstance(value, int):
        return b'i' + str(value).encode() + b'e'
    if isinstance(value, bytes):
        return str(len(value)).encode() + b':' + value
    if isinstance(value, list):
        return b'l' + b''.join(map(encode, value)) + b'e'
    return b'd' + b''.join(encode(k) + encode(v) for k, v in sorted(value.items())) + b'e'


def decode(raw):
    if raw[:2] == b"\x1f\x8b":
        raw = gzip.decompress(raw)
    position = 0
    def node():
        nonlocal position
        marker = raw[position:position+1]
        position += 1
        if marker == b'i':
            end = raw.index(b'e', position)
            value = int(raw[position:end]); position = end+1
            return value
        if marker in (b'd', b'l'):
            value = {} if marker == b'd' else []
            while raw[position:position+1] != b'e':
                if marker == b'd':
                    key = node(); value[key] = node()
                else:
                    value.append(node())
            position += 1
            return value
        position -= 1
        colon = raw.index(b':', position)
        length = int(raw[position:colon]); position = colon+1
        value = raw[position:position+length]; position += length
        return value
    result = node()
    assert position == len(raw)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--driver', type=Path, required=True)
    args = parser.parse_args()
    driver = args.driver.resolve(strict=True)
    reports = []
    with tempfile.TemporaryDirectory(prefix='biglybtc-state-cache-') as temporary:
        def fixture(name):
            profile = Path(temporary)/name
            active = profile/'active'; active.mkdir(parents=True, mode=0o700); profile.chmod(0o700)
            states = {}; entries = []
            for index in range(96):
                info = {b'name': f'generated-{index}.bin'.encode(), b'length': 3, b'piece length': 16384, b'pieces': b'x'*20}
                identity = hashlib.sha1(encode(info)).digest()
                state = {b'info': info, b'attributes': {b'old': index}, b'resume': {b'data': {b'valid': 1, b'resume data': b'\1', b'blocks': {}}}, b'future-root': b'\0\xffkeep'}
                path = active/(identity.hex().upper()+'.dat'); path.write_bytes(gzip.compress(encode(state)) if index % 2 else encode(state))
                os.utime(path, ns=(1_600_000_000_123_456_789,)*2)
                states[identity] = (state, path.stat().st_mtime_ns)
                entries.append({b'hash': identity, b'attributes': {b'new': index}, b'azp': {b'opaque': b'public'}, b'azpp': {b'opaque': b'private'}})
            # Last duplicate wins, and every duplicate for a hydrated hash disappears.
            first = next(iter(states)); entries.append({b'hash': first, b'attributes': {b'new': 999}})
            missing = b'm'*20; recovered = b'r'*20; invalid = b'i'*20
            entries.extend([{b'hash': missing, b'attributes': {}}, {b'hash': recovered, b'attributes': {}}, {b'hash': invalid, b'attributes': b'invalid'}, {b'hash': b'unknown', b'opaque': 7}, {b'future-entry': b'keep'}])
            old = next(iter(states.values()))[0]
            (active/(recovered.hex().upper()+'.dat.bak')).write_bytes(encode(old))
            (active/(invalid.hex().upper()+'.dat')).write_bytes(encode(old))
            cache = {b'state': entries, b'future-cache-root': b'\0untouched\xff'}
            raw = gzip.compress(encode(cache)); (active/'cache.dat').write_bytes(raw)
            return profile, first, states, cache, raw

        def run(profile, identity, command, expected=0):
            started = time.monotonic()
            result = subprocess.run([str(driver), str(profile), identity.hex().upper(), command], text=True, capture_output=True, timeout=30)
            assert result.returncode == expected, (command, result.returncode, result.stdout, result.stderr)
            reports.append({'case': profile.name, 'command': command, 'exit': result.returncode, 'seconds': round(time.monotonic()-started, 3), 'result': result.stdout.strip()})
            return result.stdout

        def verify(profile, states, original_cache):
            cache = decode(gzip.decompress((profile/'active/cache.dat').read_bytes()))
            assert cache[b'future-cache-root'] == original_cache[b'future-cache-root']
            assert len(cache[b'state']) == 5, cache[b'state']
            for index, (identity, (old, mtime)) in enumerate(states.items()):
                path = profile/'active'/(identity.hex().upper()+'.dat')
                current = decode(path.read_bytes())
                assert current[b'info'] == old[b'info'] and current[b'resume'] == old[b'resume']
                assert current[b'future-root'] == old[b'future-root'] and path.stat().st_mtime_ns == mtime
                assert current[b'attributes'] == {b'new': 999 if index == 0 else index}
                if index:
                    assert current[b'azureus_properties'] == {b'opaque': b'public'}
                    assert current[b'azureus_private_properties'] == {b'opaque': b'private'}
            assert not (profile/'active'/(('r'*20).encode().hex().upper()+'.dat')).exists()

        profile, first, states, cache, raw = fixture('normal')
        report = run(profile, first, 'materialize')
        assert 'hydrated=96' in report and 'state_writes=96' in report and 'cache_writes=1' in report
        verify(profile, states, cache)
        again = run(profile, first, 'materialize')
        assert 'hydrated=0' in again and 'state_writes=0' in again and 'cache_writes=0' in again
        for mode, code in [('materialize-cancel', 0), ('materialize-crash', 66), ('materialize-conflict', 0), ('materialize-pending', 0)]:
            profile, first, states, cache, raw = fixture(mode)
            run(profile, first, mode, code)
            assert (profile/'active/cache.dat').read_bytes() == raw + (b'!' if mode.endswith('conflict') else b'')
            # Every partial copy remains equivalent to Java's still authoritative cache.
            for identity, (old, mtime) in states.items():
                path = profile/'active'/(identity.hex().upper()+'.dat'); current = decode(path.read_bytes())
                assert current[b'resume'] == old[b'resume'] and current[b'info'] == old[b'info']
                assert path.stat().st_mtime_ns == mtime
            if mode.endswith('pending'):
                marker = profile/'BiglyBTC.subscription-acquire'
                assert marker.read_bytes() == b'unknown acquisition fixture; preserve exactly\n'
                marker.unlink()
            if not mode.endswith('conflict'):
                run(profile, first, 'materialize'); verify(profile, states, cache)
        print(json.dumps({'status': 'PASS', 'cases': reports}, indent=2))
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
