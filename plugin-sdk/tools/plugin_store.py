#!/usr/bin/env python3
"""Experimental offline plugin store. Explicit generated profiles and test trust only.

The production updater still needs catalogue sequence/expiry, transport policy,
key rotation, dependency qualification and application lifecycle integration.
"""
import fcntl
import hashlib
import json
import os
import pathlib
import re
import stat
import subprocess
import tempfile
import uuid

MAX_ARTIFACT = 64 * 1024 * 1024
FIELDS = {'schema', 'id', 'version', 'abi', 'platform', 'sequence', 'size', 'sha256', 'source_commit'}

def json_bytes(value):
    return (json.dumps(value, sort_keys=True, separators=(',', ':')) + '\n').encode()

def json_object(data):
    def unique(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValueError('Duplicate metadata key')
            result[key] = value
        return result
    return json.loads(data, object_pairs_hook=unique)

def regular(path, maximum):
    fd = os.open(path, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW | os.O_NONBLOCK)
    with os.fdopen(fd, 'rb') as stream:
        info = os.fstat(stream.fileno())
        if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1 or not 0 <= info.st_size <= maximum:
            raise ValueError('Expected bounded regular file')
        data = stream.read(maximum + 1)
        if len(data) > maximum:
            raise ValueError('File grew beyond bound')
        return data

def signed_manifest(manifest, signature, public_key, expected_id):
    raw = regular(manifest, 16384)
    sig = regular(signature, 64)
    if len(sig) != 64:
        raise ValueError('Expected Ed25519 signature')
    # Verify the exact bytes read, avoiding a second read of mutable inputs.
    with tempfile.TemporaryDirectory(prefix='biglybtc-signature-') as directory:
        root = pathlib.Path(directory)
        (root / 'manifest').write_bytes(raw)
        (root / 'signature').write_bytes(sig)
        (root / 'key').write_bytes(regular(public_key, 16384))
        result = subprocess.run(['openssl', 'pkeyutl', '-verify', '-pubin', '-inkey', str(root / 'key'),
            '-rawin', '-in', str(root / 'manifest'), '-sigfile', str(root / 'signature')], capture_output=True)
        if result.returncode:
            raise ValueError('Manifest signature rejected')
    item = json_object(raw)
    if set(item) != FIELDS or item['schema'] != 1 or item['id'] != expected_id:
        raise ValueError('Wrong manifest schema or original plugin ID')
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.-]{0,63}', item['id']):
        raise ValueError('Unsafe plugin ID')
    if not re.fullmatch(r'[0-9]+(?:\.[0-9]+){1,3}', item['version']):
        raise ValueError('Unsafe native version')
    if item['abi'] != 1 or item['platform'] != 'linux-amd64':
        raise ValueError('Incompatible plugin ABI/platform')
    if type(item['sequence']) is not int or item['sequence'] < 1 or type(item['size']) is not int \
            or not 0 < item['size'] <= MAX_ARTIFACT:
        raise ValueError('Invalid sequence or artifact size')
    if not re.fullmatch(r'[0-9a-f]{64}', item['sha256']) \
            or not re.fullmatch(r'[0-9a-f]{40}', item['source_commit']):
        raise ValueError('Invalid immutable artifact/source identity')
    return item, raw, sig

def safe_directory(path, create=False):
    if create:
        try:
            path.mkdir(mode=0o700)
            sync(path.parent)
        except FileExistsError:
            pass
    info = path.lstat()
    if not stat.S_ISDIR(info.st_mode) or info.st_uid != os.geteuid() or info.st_mode & 0o022:
        raise ValueError('Unsafe plugin directory')

def sync(path):
    fd = os.open(path, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC)
    try:
        os.fsync(fd)
    finally:
        os.close(fd)

def write_new(path, data):
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC | os.O_NOFOLLOW, 0o400)
    with os.fdopen(fd, 'wb') as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())

def write_pointer(directory, value):
    name = directory / ('.activate-' + uuid.uuid4().hex)
    write_new(name, json_bytes(value))
    os.replace(name, directory / 'installed.json')
    sync(directory)

class Store:
    """Lifetime holds the original .azlock. Never creates a missing Java plugin.

    Only cooperative, owner-controlled fixture directories are supported here;
    production needs descriptor-relative traversal throughout and host lease reuse.
    """
    def __init__(self, profile, folder, plugin_id, public_key):
        self.profile = pathlib.Path(profile).absolute()
        if not str(self.profile).startswith('/tmp/biglybtc-') or self.profile.resolve() != self.profile:
            raise ValueError('Only generated development profiles are accepted')
        safe_directory(self.profile)
        if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.-]{0,127}', folder):
            raise ValueError('Unsafe original directory name')
        safe_directory(self.profile / 'plugins')
        self.original = self.profile / 'plugins' / folder
        safe_directory(self.original)
        self.plugin_id = plugin_id
        self.public_key = pathlib.Path(public_key)
        self.fd = os.open(self.profile / '.azlock', os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW | os.O_CLOEXEC, 0o600)
        info = os.fstat(self.fd)
        if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1 or info.st_uid != os.geteuid():
            os.close(self.fd)
            raise ValueError('Unsafe original profile lock')
        try:
            fcntl.lockf(self.fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            # Existing Java Properties parsing/discovery belongs to the host.
            # This fixture installer requires an explicit original-ID assertion.
            descriptor = regular(self.original / 'plugin.properties', 65536).decode('iso-8859-1')
            ids = re.findall(r'^plugin\.id[ \t]*[=:][ \t]*([^\r\n]+)', descriptor, re.MULTILINE)
            if ids != [plugin_id]:
                raise ValueError('Existing original descriptor ID mismatch')
            self.native = self.original / 'biglybtc'
            safe_directory(self.native, create=True)
        except BaseException:
            os.close(self.fd)
            raise

    def __enter__(self):
        return self

    def __exit__(self, *_):
        os.close(self.fd)

    def state(self):
        try:
            data = json_object(regular(self.native / 'installed.json', 16384))
        except FileNotFoundError:
            return {'schema': 1, 'highest_sequence': 0, 'active': None, 'previous': None}
        if set(data) != {'schema', 'highest_sequence', 'active', 'previous'} or data['schema'] != 1 \
                or type(data['highest_sequence']) is not int or data['highest_sequence'] < 0:
            raise ValueError('Invalid activation pointer')
        for version in [data['active'], data['previous']]:
            if version is not None and (not isinstance(version, str)
                    or not re.fullmatch(r'[0-9]+(?:\.[0-9]+){1,3}', version)):
                raise ValueError('Unsafe activation version')
        return data

    def version_path(self, version, create=False):
        current = self.native
        for part in ['linux-amd64', '1'] + ([version] if version else []):
            current = current / part
            safe_directory(current, create=create)
        return current

    def verify_version(self, version):
        path = self.version_path(version)
        item, _, _ = signed_manifest(path / 'manifest.json', path / 'manifest.sig', self.public_key, self.plugin_id)
        if item['version'] != version:
            raise ValueError('Manifest version differs from directory')
        artifact = regular(path / 'plugin.so', MAX_ARTIFACT)
        if len(artifact) != item['size'] or hashlib.sha256(artifact).hexdigest() != item['sha256']:
            raise ValueError('Artifact integrity rejected')
        return path / 'plugin.so'

    def install(self, artifact, manifest, signature, trial, fault=None):
        item, raw, sig = signed_manifest(manifest, signature, self.public_key, self.plugin_id)
        data = regular(artifact, MAX_ARTIFACT)
        if len(data) != item['size'] or hashlib.sha256(data).hexdigest() != item['sha256']:
            raise ValueError('Artifact integrity rejected')
        state = self.state()
        if item['sequence'] <= state['highest_sequence']:
            raise ValueError('Replayed native release sequence')
        parent = self.version_path(None, create=True)
        destination = parent / item['version']
        if destination.exists() or destination.is_symlink():
            # A crash may have installed the immutable version before activation.
            self.verify_version(item['version'])
            existing = regular(destination / 'manifest.json', 16384)
            if existing != raw:
                raise ValueError('Existing version has a different identity')
        else:
            stage = parent / ('.stage-' + uuid.uuid4().hex)
            stage.mkdir(mode=0o700)
            write_new(stage / 'plugin.so', data)
            write_new(stage / 'manifest.json', raw)
            write_new(stage / 'manifest.sig', sig)
            sync(stage)
            if fault == 'staged':
                os._exit(86)
            os.rename(stage, destination)
            sync(parent)
        if fault == 'version':
            os._exit(86)
        candidate = self.verify_version(item['version'])
        trial(candidate)  # start/probe/stop in an isolated process before activation
        if fault == 'trial':
            os._exit(86)
        write_pointer(self.native, {'schema': 1, 'highest_sequence': item['sequence'],
            'active': item['version'], 'previous': state['active']})
        if fault == 'activated':
            os._exit(86)
        return candidate

    def active(self):
        state = self.state()
        return self.verify_version(state['active']) if state['active'] else None

    def rollback(self, trial):
        state = self.state()
        if not state['previous']:
            raise ValueError('No previous version')
        candidate = self.verify_version(state['previous'])
        trial(candidate)
        state['active'], state['previous'] = state['previous'], state['active']
        # Keep highest_sequence after rollback: old network releases stay rejected.
        write_pointer(self.native, state)
        return candidate

    def uninstall(self):
        # Caller must have stopped/unloaded its instances. Publish a tombstone
        # before deleting authenticated native files; never recursively remove
        # unknown content or Java artifacts. Crash remnants are retained for review.
        state = self.state()
        state['active'] = state['previous'] = None
        write_pointer(self.native, state)
        try:
            parent = self.version_path(None)
        except FileNotFoundError:
            return
        for candidate in parent.iterdir():
            if not re.fullmatch(r'[0-9]+(?:\.[0-9]+){1,3}', candidate.name):
                continue
            try:
                self.verify_version(candidate.name)
            except (ValueError, OSError):
                continue
            for name in ['plugin.so', 'manifest.sig', 'manifest.json']:
                (candidate / name).unlink()
            sync(candidate)
            if not any(candidate.iterdir()):
                candidate.rmdir()
            sync(parent)

if __name__ == '__main__':
    raise SystemExit('Use the host API with an explicit generated profile, trust key and activation trial.')
