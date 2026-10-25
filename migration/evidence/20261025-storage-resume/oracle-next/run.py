#!/usr/bin/env python3
"""Separate actual-original root/default/negative-link handoffs; root executes."""
import argparse
import gzip
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import time

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
ORIGINAL = 'cb8c6cb9bb97916cbc7e80338a017200ffc3e71f'
JAVA = Path('/usr/lib/jvm/java-17-openjdk-amd64/bin')

# Literal input variations, not expected original roots or policy outputs.
CASES = (
    ('single-path-directory', 'single', 'path-directory', 'absent', 'absent', None),
    ('single-path-file', 'single', 'path-file', 'absent', 'empty', None),
    ('single-path-missing', 'single', 'path-missing', 'absent', 'absent', None),
    ('single-save-dir-missing', 'single', 'save-missing', 'absent', 'empty', None),
    ('multi-path-parent', 'multi', 'path-directory', 'disabled', 'absent', None),
    ('multi-path-existing-name', 'multi', 'path-existing-name', 'absent', 'empty', None),
    ('multi-path-missing-name', 'multi', 'path-missing-name', 'absent', 'absent', None),
    ('multi-save-dir-parent', 'multi', 'save-directory', 'absent', 'empty', None),
    ('multi-global-reorder', 'multi', 'explicit', 'reorder', 'absent', None),
    ('multi-saved-reorder', 'multi', 'explicit', 'saved-reorder', 'empty', None),
    ('negative-one-external', 'multi', 'explicit', 'absent', 'absent', -1),
    ('negative-minimum-external', 'multi', 'explicit', 'reorder', 'empty', -2147483648),
)


def require(value, reason):
    if not value:
        raise ValueError(reason)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def regular(path):
    s = path.lstat()
    require(stat.S_ISREG(s.st_mode) and s.st_uid == os.geteuid() and s.st_nlink == 1,
            'Unsafe owned input: ' + str(path))
    return s


def file_map(root):
    result = {}
    for path in sorted(root.rglob('*')):
        require(not path.is_symlink(), 'Symbolic fixture/cohort entry')
        if path.is_file():
            regular(path)
            result[path.relative_to(root).as_posix()] = sha(path)
    return result


def payload_map(case):
    """A no-initialization original probe may not change path existence/data."""
    result = {}
    for name in ('payload', 'external'):
        root = case / name
        if not root.exists():
            continue
        for path in [root, *sorted(root.rglob('*'))]:
            require(not path.is_symlink(), 'Symbolic generated payload')
            s = path.lstat()
            require(s.st_uid == os.geteuid(), 'Unowned generated payload')
            key = path.relative_to(case).as_posix()
            if path.is_dir():
                result[key] = 'directory'
            else:
                regular(path)
                result[key] = sha(path)
    return result


def import_helper(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def encode(value):
    # Authored ASCII-key fixture fields only. Untouched subject values use raw
    # spans below; production parsing/decisions are never implemented here.
    if isinstance(value, int):
        return b'i' + str(value).encode() + b'e'
    if isinstance(value, bytes):
        return str(len(value)).encode() + b':' + value
    if isinstance(value, list):
        return b'l' + b''.join(encode(x) for x in value) + b'e'
    require(isinstance(value, dict), 'Unsupported authored fixture field')
    return b'd' + b''.join(encode(k) + encode(v) for k, v in sorted(value.items())) + b'e'


def rows(text):
    result = {'files': {}}
    for line in text.splitlines():
        if not line.startswith('MAPPING_'):
            continue
        fields = line.split('\t')
        key = fields[0]
        if key == 'MAPPING_FILE':
            require(len(fields) == 8 and fields[3] in ('true', 'false'), 'Malformed original file observation')
            index = int(fields[1])
            require(index not in result['files'], 'Duplicate original file observation')
            result['files'][index] = {'kind': int(fields[2]), 'skipped': fields[3] == 'true',
                'priority': int(fields[4]), 'bytes': int(fields[5]),
                'base': bytes.fromhex(fields[6]).decode('utf-8'),
                'actual': bytes.fromhex(fields[7]).decode('utf-8')}
        elif key in ('MAPPING_ROOT', 'MAPPING_HASH', 'MAPPING_TYPES', 'MAPPING_THRESHOLD'):
            require(len(fields) == 2 and key not in result, 'Duplicate/malformed original scalar observation')
            result[key] = bytes.fromhex(fields[1]).decode('utf-8') if key == 'MAPPING_ROOT' else fields[1]
        elif key in ('MAPPING_COUNTERS', 'MAPPING_VERIFIED'):
            require(len(fields) == 3 and key not in result, 'Duplicate/malformed original numeric observation')
            result[key] = tuple(map(int, fields[1:]))
        elif key == 'MAPPING_STOPPED':
            require(len(fields) == 1 and key not in result, 'Malformed original stopped marker')
            result[key] = True
        else:
            raise ValueError('Unknown original observation marker')
    needed = {'files', 'MAPPING_ROOT', 'MAPPING_HASH', 'MAPPING_TYPES', 'MAPPING_THRESHOLD',
              'MAPPING_COUNTERS', 'MAPPING_STOPPED'}
    require(needed <= set(result) and bool(result['files']), 'Incomplete original observations')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference-root', type=Path, required=True)
    parser.add_argument('--classes', type=Path, default=Path('/tmp/biglybtc-oracle/classes'))
    parser.add_argument('--retention', type=Path,
        default=Path('/home/yurii/Java/BiglyBTC/build/dependencies/java-oracle-17-20261021/retention.json'))
    parser.add_argument('--driver', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--case', choices=[c[0] for c in CASES], action='append')
    args = parser.parse_args()
    manifest = json.loads((HERE / 'manifest.json').read_text())
    for name, digest in manifest['files'].items():
        require(sha(HERE / name) == digest, 'Changed frozen oracle-next input: ' + name)
    provenance = json.loads((HERE / 'source-provenance.json').read_text())
    reference = args.reference_root.resolve(strict=True)
    for name, digest in provenance['reference_sha256'].items():
        require(sha(reference / name) == digest, 'Changed retained helper: ' + name)
    for name, data in provenance['original_sources'].items():
        require(sha(reference / name) == data['sha256'], 'Changed pinned original source: ' + name)
    require(sha(args.retention) == provenance['java_retention_sha256'], 'Changed actual class retention receipt')
    retained = json.loads(args.retention.read_text())
    require(retained['source_commit'] == ORIGINAL and retained['recompiled'] is False, 'Wrong original class cohort')
    classes = args.classes.resolve(strict=True)
    require(classes == args.classes and classes.is_relative_to('/tmp') and file_map(classes) == retained['file_sha256'],
            'Original compiled class/resource bytes differ')
    driver = args.driver.resolve(strict=True)
    require(regular(driver).st_mode & stat.S_IXUSR, 'Native driver is not executable')
    work = args.work.absolute()
    require(work.parent == Path('/tmp') and work.name.startswith('biglybtc-managed-handoff-defaults-')
            and work.resolve() == work and not work.exists() and not work.is_symlink(), 'Fresh canonical owned /tmp root required')
    work.mkdir(mode=0o700)
    (work / 'classes').mkdir(mode=0o700)
    (work / 'empty-sourcepath').mkdir(mode=0o700)
    env = {k: v for k, v in os.environ.items()
           if k not in ('CLASSPATH', 'JAVA_TOOL_OPTIONS', '_JAVA_OPTIONS', 'JDK_JAVA_OPTIONS', 'LD_PRELOAD')}
    env['PYTHONDONTWRITEBYTECODE'] = '1'
    commands = []

    def run(name, command, cwd, timeout):
        item = {'name': name, 'argv': list(map(str, command)), 'cwd': str(cwd), 'timeout': timeout}
        commands.append(item)
        (work / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
        with (work / (name + '.stdout')).open('wb') as stdout, (work / (name + '.stderr')).open('wb') as stderr:
            process = subprocess.Popen(item['argv'], cwd=cwd, env=env, stdout=stdout, stderr=stderr)
            deadline = time.monotonic() + timeout
            try:
                while True:
                    try:
                        code = process.wait(timeout=min(40, max(0.01, deadline - time.monotonic())))
                        break
                    except subprocess.TimeoutExpired:
                        require(time.monotonic() < deadline, 'Owned helper deadline exceeded: ' + name)
                        print('still running ' + name, flush=True)
                require(code == 0, 'Owned helper failed; raw logs retained: ' + name)
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait(timeout=10)
        return (work / (name + '.stdout')).read_text()

    sources = [reference / 'migration/fixtures/java' / n for n in ('ReferenceFence.java', 'EngineOracle.java')]
    sources.append(HERE / 'StorageDefaultsOracle.java')
    run('compile-fixture-helpers', [JAVA / 'javac', '-J-XX:ActiveProcessorCount=2', '--release', '8', '-encoding', 'UTF-8',
        '-proc:none', '-implicit:none', '-sourcepath', work / 'empty-sourcepath', '-cp', classes,
        '-d', work / 'classes', *sources], work, 60)
    sys.path.insert(0, str(reference / 'migration/tools'))
    peer = import_helper('defaults_peer_interop', reference / 'migration/tools/peer_interop.py')
    handoff = import_helper('defaults_fixture_decode', reference / 'migration/tools/handoff.py')
    classpath = str(work / 'classes') + os.pathsep + str(classes)

    def java(label, case, phase, shape):
        port = peer.unused_port()
        text = run(label, [*peer.jvm(case, classpath, (port,)), 'StorageDefaultsOracle', port, phase, shape], case, 150)
        observed = rows(text)
        require(len(observed['files']) == (1 if shape == 'single' else 3), 'Wrong original fixture shape')
        return observed

    def read_doc(path):
        data = path.read_bytes()
        raw = gzip.decompress(data) if data.startswith(b'\x1f\x8b') else data
        require(len(raw) <= 4 * 1024 * 1024, 'Generated document exceeds fixture bound')
        return handoff.decode(raw)

    def rewrite(path, changes):
        compressed = path.read_bytes().startswith(b'\x1f\x8b')
        data, spans = read_doc(path)
        require(isinstance(data, dict), 'Generated document is not a dictionary')
        raw = b'd'
        for key in sorted(set(data) | set(changes)):
            if key in changes:
                if changes[key] is not None:
                    raw += encode(key) + encode(changes[key])
            else:
                raw += encode(key) + spans[(key,)]
        raw += b'e'
        path.write_bytes(gzip.compress(raw, mtime=0) if compressed else raw)

    result = {'status': 'RUNNING', 'original_commit': ORIGINAL, 'original_core_recompiled': False,
        'manifest_sha256': sha(HERE / 'manifest.json'), 'retention_sha256': sha(args.retention),
        'driver': {'path': str(driver), 'sha256': sha(driver)}, 'cases': {},
        'original_profile_or_process_access': False}
    (work / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    chosen = set(args.case or [c[0] for c in CASES])
    for name, shape, root_form, policy, stores, negative in CASES:
        if name not in chosen:
            continue
        case = work / name
        case.mkdir(mode=0o700)
        created = java(name + '-prepare', case, 'prepare', shape)
        profile = case / 'profile'
        # Preserve authentic original exports before the explicitly authored
        # legacy/default variations. The subject methods remain unchanged.
        file_map(profile)
        shutil.copytree(profile, case / 'original-generated-profile')
        inventory, _ = read_doc(profile / 'downloads.config')
        require(len(inventory[b'downloads']) == 1, 'Expected one actual original download')
        row = inventory[b'downloads'][0]
        name_bytes = b'single.bin' if shape == 'single' else b'layout'
        base = case / 'payload'
        if root_form in ('path-directory', 'save-directory'):
            selected_root = base / 'selected-parent'
            selected_root.mkdir()
        elif root_form == 'path-existing-name':
            selected_root = base / 'layout'
            selected_root.mkdir()
        elif root_form == 'path-file':
            selected_root = base / 'renamed.bin'
            selected_root.write_bytes(b'')
        elif root_form in ('path-missing', 'save-missing'):
            selected_root = base / 'not-created.bin'
        elif root_form == 'path-missing-name':
            selected_root = base / 'missing' / 'layout'
        else:
            require(root_form == 'explicit', 'Unknown literal root form')
            selected_root = base / 'explicit'
            selected_root.mkdir()
        row.pop(b'path', None)
        row.pop(b'save_dir', None)
        row.pop(b'save_file', None)
        if root_form == 'explicit':
            row[b'save_dir'] = str(selected_root).encode()
            row[b'save_file'] = name_bytes
        else:
            row[b'save_dir' if root_form.startswith('save-') else b'path'] = str(selected_root).encode()
        row[b'native.defaults.row'] = [-7, b'raw-preserved']
        rewrite(profile / 'downloads.config', {b'downloads': [row], b'native.defaults.inventory': [b'raw', -11]})
        state = profile / 'active' / (created['MAPPING_HASH'] + '.dat')
        current, before_spans = read_doc(state)
        attrs = current.get(b'attributes', {}).copy()
        attrs.pop(b'storetypes', None)
        if stores == 'empty':
            attrs[b'storetypes'] = []
        attrs.pop(b'reordermb', None)
        attrs.pop(b'filelinks2', None)
        attrs.pop(b'incompfilesuffix', None)
        attrs[b'native.defaults.attributes'] = [b'opaque', -3]
        if policy == 'saved-reorder':
            attrs[b'reordermb'] = 0
        if negative is not None:
            external = case / 'external' / 'reported.bin'
            external.parent.mkdir()
            external.write_bytes(b'')
            original_base = selected_root / 'layout/payload.bin'
            attrs[b'filelinks2'] = [str(negative).encode() + b'\n' + str(original_base).encode() + b'\n' + str(external).encode()]
        rewrite(state, {b'attributes': attrs})
        backup = Path(str(state) + '.bak')
        if backup.exists():
            regular(backup)
            backup.write_bytes(state.read_bytes())
        for suffix in ('', '.bak', '.saving'):
            cache = profile / 'active' / ('cache.dat' + suffix)
            if cache.exists():
                regular(cache)
                cache.unlink()  # This generated cache must not override authored primary attributes.
            config = profile / ('biglybt.config' + suffix)
            if config.exists():
                regular(config)
                config.unlink()
        if policy != 'absent':
            settings = {b'Enable reorder storage mode': 0 if policy == 'disabled' else 1,
                b'Reorder storage mode min MB': 100 if policy == 'saved-reorder' else 0}
            (profile / 'biglybt.config').write_bytes(encode(settings))
        require(read_doc(state)[1][(b'info',)] == before_spans[(b'info',)], 'Fixture authoring changed hashed metadata')
        authored = file_map(profile)
        shutil.copytree(profile, case / 'authored-profile')
        payload_before_probe = payload_map(case)
        probe = java(name + '-probe-original', case, 'probe', shape)
        require(payload_map(case) == payload_before_probe, 'Original load-only probe changed payload existence/data')
        require(probe['MAPPING_HASH'] == created['MAPPING_HASH'], 'Original probe changed torrent identity')
        # Keep the probe's actual canonical export, then restore EXACT authored
        # inputs so native, rather than the probe's Java save, must handle them.
        profile.rename(case / 'original-probe-profile')
        shutil.copytree(case / 'authored-profile', profile)
        require(file_map(profile) == authored, 'Authored input restoration differs')
        before_inventory, _ = read_doc(profile / 'downloads.config')
        previous = before_inventory[b'downloads'][0]
        native_config = (profile / 'biglybt.config').read_bytes() if (profile / 'biglybt.config').exists() else None
        expected = case / ('expected/single.bin' if shape == 'single' else 'expected/layout')
        output = case / 'native-complete'
        run(name + '-native', [driver, work, profile, case / 'input.torrent', expected, output, 'complete'], case, 120)
        receipt = json.loads((output / 'result.json').read_text())
        require(receipt['hash'] == created['MAPPING_HASH'] and receipt['complete'] is True
                and receipt['pieces'] == receipt['total_pieces'] == 8 and int(receipt['received']) > 0
                and receipt['session_joined'] is True and receipt['storage_released'] is True,
                'Missing actual transfer/joined storage handoff')
        require(((profile / 'biglybt.config').read_bytes() if (profile / 'biglybt.config').exists() else None) == native_config,
                'Native changed source default settings')
        after, _ = read_doc(profile / 'downloads.config')
        native_row = after[b'downloads'][0]
        actual_root = Path(native_row[b'save_dir'].decode()) / native_row[b'save_file'].decode()
        require(str(actual_root) == probe['MAPPING_ROOT'], 'Native root differs from actual original load decision')
        require(native_row[b'native.defaults.row'] == previous[b'native.defaults.row']
                and after[b'native.defaults.inventory'] == before_inventory[b'native.defaults.inventory'],
                'Native lost opaque original row/root values')
        require(native_row[b'downloaded'] == previous.get(b'downloaded', 0) + int(receipt['received'])
                and native_row[b'uploaded'] == previous.get(b'uploaded', 0) + int(receipt['sent']),
                'Native admission totals were not added exactly once')
        current, spans = read_doc(state)
        require(spans[(b'native.defaults.opaque',)] == before_spans[(b'native.defaults.opaque',)]
                and current[b'attributes'][b'native.defaults.attributes'] == attrs[b'native.defaults.attributes'],
                'Native changed opaque state bytes/attributes')
        native_types = current[b'attributes'][b'storetypes']
        type_number = {b'L': 1, b'C': 2, b'R': 3, b'X': 4}
        require([type_number[t] for t in native_types] == [probe['files'][i]['kind'] for i in sorted(probe['files'])],
                'Native requested storage defaults differ from actual original getters')
        if probe['MAPPING_THRESHOLD'] != 'absent':
            require(current[b'attributes'].get(b'reordermb') == int(probe['MAPPING_THRESHOLD']),
                    'Native captured reorder threshold differs from actual original getter')
        observed = java(name + '-observe-original', case, 'observe', shape)
        require(observed['MAPPING_VERIFIED'] == (8, 131072)
                and observed['MAPPING_ROOT'] == probe['MAPPING_ROOT']
                and observed['MAPPING_HASH'] == probe['MAPPING_HASH'], 'Original reload lost native root/verified bytes')
        require(observed['MAPPING_COUNTERS'] == (native_row[b'downloaded'], native_row[b'uploaded']),
                'Original reload lost actual native cumulative counters')
        for index in probe['files']:
            require(observed['files'][index]['actual'] == probe['files'][index]['actual']
                    and not observed['files'][index]['skipped'] and observed['files'][index]['priority'] == 0,
                    'Original reload changed actual file-link identity/normal selection')
        if negative is not None:
            require(probe['files'][1]['actual'] == str(case / 'external/reported.bin'),
                    'Original did not resolve the actual signed-negative external link')
        result['cases'][name] = {'authored_profile_sha256': authored, 'original_probe': probe,
            'native': receipt, 'original_reload': observed, 'final_profile_sha256': file_map(profile)}
        (work / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    require(len(result['cases']) == len(chosen), 'Requested bounded cases not completed')
    require(file_map(classes) == retained['file_sha256'], 'Retained original classes changed during run')
    result.update(status='PASS_ACTUAL_ORIGINAL_SAVE_DEFAULT_HANDOFF', actual_cases=len(result['cases']),
        authored_class_sha256=file_map(work / 'classes'), commands=commands,
        log_sha256={p.name: sha(p) for p in sorted(work.glob('*.std*'))},
        limitations=['No original rebuild/live profile/process/public peers',
            'Only generated single/three-file 128KiB v1 root/default/link cases',
            'No arbitrary encoding, symbolic-link or fifth storage-format equality claim'])
    (work / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({'status': result['status'], 'actual_cases': result['actual_cases'],
        'manifest_sha256': result['manifest_sha256'], 'result': str(work / 'result.json')}), flush=True)


if __name__ == '__main__':
    main()
