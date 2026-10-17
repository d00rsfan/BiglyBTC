"""Actual signed shared-object install/activation/crash/rollback in owned profiles."""
import hashlib
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

sdk = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(sdk / 'tools'))
from plugin_store import Store, json_bytes

driver, module, fixture = map(pathlib.Path, sys.argv[1:])
root = pathlib.Path(tempfile.mkdtemp(prefix='biglybtc-store-'))
profile = root / 'profile'
original = profile / 'plugins' / 'LocationProvider'
original.mkdir(parents=True)
for path in [profile, profile / 'plugins', original]:
    path.chmod(0o700)
(profile / 'azureus.config').write_bytes(b'd28:PluginInfo.azlocprov.enabledi0e6:opaque4:\x00\xff\x80\x7fe')
(original / 'plugin.properties').write_bytes(b'plugin.id=azlocprov\nplugin.version=0.6.9\n')
(original / 'azlocprov_0.6.9.jar').write_bytes(b'untouched fixture Java jar')
shutil.copyfile(fixture, original / 'GeoLite2-Country.mmdb')
before = {p.relative_to(profile).as_posix(): (p.stat().st_ino, p.read_bytes())
    for p in profile.rglob('*') if p.is_file()}
def java_layout():
    if os.environ.get('BIGLYBTC_LAYOUT_ORACLE_COMMAND'):
        command = json.loads(os.environ['BIGLYBTC_LAYOUT_ORACLE_COMMAND'])
        subprocess.run([arg.replace('@ROOT@', str(root)) for arg in command] + [str(profile), str(original)],
            check=True, capture_output=True)
        print('PASS frozen Java config and JAR selectors before/after native changes')
java_layout()
private = root / 'ephemeral-test-private.pem'
public = root / 'ephemeral-test-public.pem'
def command(*args):
    subprocess.run(args, check=True, capture_output=True)
command('openssl', 'genpkey', '-algorithm', 'Ed25519', '-out', str(private))
private.chmod(0o600)
command('openssl', 'pkey', '-in', str(private), '-pubout', '-out', str(public))
def release(tag, sequence, **changes):
    manifest = root / f'{tag}-{sequence}-{len(list(root.iterdir()))}.json'
    minor = int(tag.split('.')[1])
    candidate = module if minor < 2 else module.with_name(f'location-update-{minor}.so')
    data = candidate.read_bytes()
    item = {'schema': 1, 'id': 'azlocprov', 'version': tag, 'sequence': sequence,
        'abi': 1, 'platform': 'linux-amd64', 'size': len(data),
        'sha256': hashlib.sha256(data).hexdigest(), 'source_commit': 'a' * 40}
    item.update(changes)  # synthetic source identity, never release metadata
    manifest.write_bytes(json_bytes(item))
    signature = manifest.with_suffix('.sig')
    command('openssl', 'pkeyutl', '-sign', '-inkey', str(private), '-rawin', '-in', str(manifest), '-out', str(signature))
    return candidate, manifest, signature

def trial(path):
    environment = os.environ.copy()
    environment['BIGLYBTC_EXPECTED_NATIVE_VERSION'] = path.parent.name
    result = subprocess.run([str(driver), str(path), str(original), '0.6.9', '81.2.69.160', 'en'],
        capture_output=True, env=environment)
    if result.returncode or result.stdout != b'GB\tUnited Kingdom\n':
        raise ValueError('Activation trial failed: ' + repr(result.stderr))

def reject(store, files, callback=trial):
    snapshot = (store.native / 'installed.json').read_bytes() if (store.native / 'installed.json').exists() else None
    try:
        store.install(*files, callback)
        raise AssertionError('Invalid candidate accepted')
    except ValueError:
        pass
    assert ((store.native / 'installed.json').read_bytes() if (store.native / 'installed.json').exists() else None) == snapshot

with Store(profile, 'LocationProvider', 'azlocprov', public) as store:
    first = store.install(*release('0.1.0', 1), trial)
    assert store.active() == first
    reject(store, release('0.2.0', 2, id='mldht'))
    reject(store, release('0.2.0', 2, abi=99))
    reject(store, release('0.2.0', 2, platform='windows-amd64'))
    reject(store, release('0.2.0', 2, version='../escape'))
    reject(store, release('0.2.0', 2, sha256='f' * 64))
    reject(store, release('0.1.0', 2, version='0.9.0'))  # signed metadata must match the actual loaded module
    tampered = release('0.2.0', 2)
    tampered[1].write_bytes(tampered[1].read_bytes() + b' ')
    reject(store, tampered)
    reject(store, release('0.0.1', 1))
    def failed_trial(_):
        raise ValueError('Missing dependency / failed startup')
    reject(store, release('0.2.0', 2), failed_trial)
    assert store.active() == first

# A separate process cannot acquire the shared profile while Java/native holds it.
with Store(profile, 'LocationProvider', 'azlocprov', public):
    child = os.fork()
    if child == 0:
        try:
            with Store(profile, 'LocationProvider', 'azlocprov', public):
                os._exit(1)
        except BlockingIOError:
            os._exit(0)
    assert os.waitpid(child, 0)[1] == 0

# Interrupt at each persistent boundary, reopen, and retry exact immutable release.
for index, stage in enumerate(['staged', 'version', 'trial', 'activated'], 3):
    files = release(f'0.{index}.0', index)
    with Store(profile, 'LocationProvider', 'azlocprov', public) as store:
        prior = store.state()['active']
    child = os.fork()
    if child == 0:
        with Store(profile, 'LocationProvider', 'azlocprov', public) as store:
            store.install(*files, trial, fault=stage)
        os._exit(1)
    assert os.waitpid(child, 0)[1] == 86 << 8
    with Store(profile, 'LocationProvider', 'azlocprov', public) as store:
        assert store.state()['active'] == (f'0.{index}.0' if stage == 'activated' else prior)
        trial(store.active())
        if stage != 'activated':
            store.install(*files, trial)
        assert store.state()['active'] == f'0.{index}.0'
        assert store.state()['previous'] == prior
        store.rollback(trial)
        assert store.state()['active'] == prior
        assert store.state()['highest_sequence'] == index
        reject(store, files)

with Store(profile, 'LocationProvider', 'azlocprov', public) as store:
    unknown = store.version_path('0.1.0') / 'foreign.txt'
    unknown.write_bytes(b'preserve unknown native-adjacent data')
    store.uninstall()
    assert store.active() is None
    assert unknown.read_bytes() == b'preserve unknown native-adjacent data'
    assert not (unknown.parent / 'plugin.so').exists()
for name, (inode, data) in before.items():
    assert (profile / name).stat().st_ino == inode
    assert (profile / name).read_bytes() == data
java_layout()
# Original uninstall/missing folder is respected, even with stale enabled config.
original.rename(original.with_name('RemovedByJava'))
try:
    with Store(profile, 'LocationProvider', 'azlocprov', public):
        raise AssertionError('Missing original plugin recreated')
except FileNotFoundError:
    pass
assert not original.exists()
print('PASS signed activation, 4 real crash boundaries, rollback, replay, lease, native uninstall and original bytes/inodes:', root)
