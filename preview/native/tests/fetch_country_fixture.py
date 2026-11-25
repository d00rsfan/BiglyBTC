"""Fetch pinned official synthetic MaxMind country and ASN fixtures."""
import hashlib
import pathlib
import sys
import urllib.request

COMMIT = '0ee49278e35fd7bdf341ac236edbba20de1aceb9'


def fetch(target, name, expected):
    if target.exists():
        data = target.read_bytes()
    else:
        url = f'https://raw.githubusercontent.com/maxmind/MaxMind-DB/{COMMIT}/test-data/{name}'
        with urllib.request.urlopen(url, timeout=30) as response:
            data = response.read(1024 * 1024)
    if hashlib.sha256(data).hexdigest() != expected:
        raise ValueError(f'Official test fixture identity differs for {name}')
    target.parent.mkdir(parents=True, exist_ok=True)
    if not target.exists():
        with target.open('xb') as stream:
            stream.write(data)
    print('Verified synthetic MaxMind fixture', name, expected)


country = pathlib.Path(sys.argv[1])
fetch(country, 'GeoIP2-Country-Test.mmdb',
      'ff5a273c91d332bcfb4d808cc128c4f98cb59bb55f75931b64ff69022ddfa1bc')
fetch(country.parent / 'GeoLite2-ASN-Test.mmdb', 'GeoLite2-ASN-Test.mmdb',
      '5f2da5602a6947dae10486fe337d6112d2e37eaa2bc8d32e1fdeeeae645a3e4a')
