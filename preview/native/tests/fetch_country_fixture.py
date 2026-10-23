"""Fetch only the pinned official synthetic country test database, never GeoLite."""
import hashlib
import pathlib
import sys
import urllib.request
commit = '0ee49278e35fd7bdf341ac236edbba20de1aceb9'
expected = 'ff5a273c91d332bcfb4d808cc128c4f98cb59bb55f75931b64ff69022ddfa1bc'
target = pathlib.Path(sys.argv[1])
if target.exists():
    data = target.read_bytes()
else:
    url = f'https://raw.githubusercontent.com/maxmind/MaxMind-DB/{commit}/test-data/GeoIP2-Country-Test.mmdb'
    data = urllib.request.urlopen(url, timeout=30).read(1024 * 1024)
if hashlib.sha256(data).hexdigest() != expected:
    raise ValueError('Official test fixture identity differs')
target.parent.mkdir(parents=True, exist_ok=True)
with target.open('xb') if not target.exists() else target.open('rb') as stream:
    if stream.writable():
        stream.write(data)
print('Verified synthetic MaxMind fixture', expected)
