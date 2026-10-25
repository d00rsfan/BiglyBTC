#!/usr/bin/env python3
"""Stage/qualify an already-built final source; never build or promote here."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import tempfile
import time
import xml.etree.ElementTree as ET

NATIVE = Path('/home/yurii/Java/BiglyBTC')
HERE = Path(__file__).resolve().parent
DSO_SHA = 'c35f1b0408508c019f002c438d70dd361a55be20286ded56711efd6bba2424e3'

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def manifest(directory):
    raw = directory / 'BUILD_MANIFEST.json'
    data = json.loads(raw.read_text())
    for name, expected in data['files_sha256'].items():
        path = directory / name
        assert not Path(name).is_absolute() and '..' not in Path(name).parts
        assert path.resolve(strict=True).is_relative_to(directory.resolve()), name
        assert path.is_file() and sha(path) == expected, name
    actual = {str(p.relative_to(directory)) for p in directory.rglob('*') if p.is_file()}
    assert actual == set(data['files_sha256']) | {'BUILD_MANIFEST.json'}
    return sha(raw), data

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source', required=True)
    p.add_argument('--source-map', required=True, type=Path)
    p.add_argument('--build', required=True, type=Path)
    p.add_argument('--attempt', required=True)
    a = p.parse_args()
    assert re.fullmatch('[0-9a-f]{40}', a.source)
    assert re.fullmatch('[A-Za-z0-9_-]+', a.attempt)
    baseline = json.loads((HERE / 'baseline.json').read_text())
    plan = json.loads((HERE / 'plan.json').read_text())
    expected = json.loads(a.source_map.read_text())
    assert expected and all(isinstance(k, str) and re.fullmatch('[0-9a-f]{64}', v) for k, v in expected.items())
    def source():
        assert subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=NATIVE, text=True).strip() == a.source
        actual = {str(v.relative_to(NATIVE)) for v in (NATIVE / 'preview/native').rglob('*') if v.is_file()}
        assert actual == set(expected), 'Source inventory changed after the final compiler receipt'
        assert subprocess.run(['git', 'diff', '--quiet', a.source, '--', 'preview/native'], cwd=NATIVE).returncode == 0
        for name, digest in expected.items():
            assert name.startswith('preview/native/') and '..' not in Path(name).parts
            assert sha(NATIVE / name) == digest, name
    source()
    old, _ = manifest(NATIVE / 'dist')
    assert old == baseline['previous_dist_manifest_sha256'], 'Published dist changed; freeze an explicit new baseline first'
    for name, digest in baseline['tool_sha256'].items():
        assert sha(Path(name)) == digest, name
    build = a.build.resolve(strict=True)
    assert build.is_relative_to(NATIVE / 'build')
    cache = (build / 'CMakeCache.txt').read_text()
    assert 'BIGLYBTC_BUNDLE_QT:BOOL=ON' in cache
    assert 'BIGLYBTC_PERSISTENT_RUNTIME_MANIFEST:FILEPATH=' in cache
    assert '/opt/Qt/6.12.0/gcc_64/lib/cmake/Qt6' in cache
    qt = json.loads((build / 'qt-runtime.json').read_text())
    assert qt['qt_version'] == '6.12.0' and qt['variant'] == 'bundled-qt'
    catalog = json.loads(subprocess.check_output(['ctest', '--test-dir', str(build), '--show-only=json-v1'], text=True))
    selected = {t['name']: t for t in catalog['tests'] if t['name'] in plan['tests']}
    assert set(selected) == set(plan['tests']), 'Missing registered chapter tests'
    assert all(Path(selected[name]['command'][0]).name == plan['test_targets'][name] for name in plan['tests'])
    targets = list(dict.fromkeys(plan['products'] + list(plan['test_targets'].values())))
    dry = subprocess.check_output(['cmake', '--build', str(build), '--target', *targets, '--', '-n'], text=True, cwd=NATIVE)
    assert 'no work to do' in dry.lower(), 'Finish the cohesive build first: stale static executable(s)'
    publication = NATIVE / 'build/storage-20261025/portable' / a.attempt
    publication.mkdir(parents=True, exist_ok=False)
    out = publication / 'verification'; out.mkdir()
    candidate = publication / 'candidate'
    (out / 'current-binaries-dry-run.txt').write_text(dry)
    (out / 'source-map.json').write_bytes(a.source_map.read_bytes())
    rows = []
    env = os.environ.copy()
    # No inherited capture/handoff/provider controls may retarget these tests.
    for key in tuple(env):
        if key.startswith('BIGLYBTC_') or key in ('LD_PRELOAD', 'QT_SCALE_FACTOR', 'QT_AUTO_SCREEN_SCALE_FACTOR'):
            env.pop(key)
    def run(name, argv, timeout, expected_exit=0):
        started = time.monotonic()
        timed_out = False
        with (out / (name + '.log')).open('x') as stream:
            result = subprocess.Popen(argv, cwd=NATIVE, env=env, stdout=stream, stderr=subprocess.STDOUT, start_new_session=True)
            while True:
                remaining = timeout - (time.monotonic() - started)
                if remaining <= 0:
                    timed_out = True
                    try:
                        os.killpg(result.pid, signal.SIGTERM)
                    except ProcessLookupError:
                        pass
                    try:
                        result.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        os.killpg(result.pid, signal.SIGKILL); result.wait()
                    break
                try:
                    result.wait(timeout=min(45, remaining)); break
                except subprocess.TimeoutExpired:
                    print(name + ' running; full output stays in ' + str(out / (name + '.log')), flush=True)
        rows.append({'name': name, 'argv': argv, 'exit_code': result.returncode,
            'expected_exit': expected_exit, 'succeeded': result.returncode == expected_exit and not timed_out,
            'timed_out': timed_out, 'seconds': time.monotonic() - started})
        (out / 'commands.json').write_text(json.dumps(rows, indent=2) + '\n')
        print(name + (' FAILED' if result.returncode != expected_exit or timed_out else ' passed'), flush=True)
        assert result.returncode == expected_exit and not timed_out, name
    run('stage', ['python3', str(NATIVE / 'preview/migration/tools/stage_preview.py'), '--build', str(build),
        '--output', str(candidate), '--libtorrent-package', baseline['libtorrent_package'],
        '--source-libtorrent', baseline['source_libtorrent']], 240)
    digest, bundle = manifest(candidate)
    provider = bundle['libtorrent_runtime']
    assert provider['artifact_sha256'] == DSO_SHA and provider['artifact_bytes'] == 8737560 and not provider['instrumented']
    normal_launcher = candidate / 'BiglyBTC'
    assert normal_launcher.is_file() and not normal_launcher.is_symlink() and os.access(normal_launcher, os.X_OK)
    assert normal_launcher.read_bytes() == (HERE / 'normal-launcher.txt').read_bytes()
    assert bundle['normal_launch'] == './BiglyBTC --profile ABSOLUTE_PROFILE'
    assert sha(candidate / 'BiglyBTC-preview') == baseline['offline_launcher_sha256']
    private = candidate / 'usr/lib/biglybtc'
    assert sha(private / 'lib/libtorrent-rasterbar.so.2.0.12') == DSO_SHA
    assert (private / 'qt/plugins/platforms/libqoffscreen.so').is_file()
    scheduler_name = 'BiglyBTC-plugin-SpeedScheduler.so'
    scheduler_built = build / 'app' / scheduler_name
    scheduler_staged = private / 'lib' / scheduler_name
    assert scheduler_staged.is_file() and sha(scheduler_staged) == sha(scheduler_built)
    approval = (build / 'app/generated/scheduler_approval.hpp').read_text()
    assert sha(scheduler_staged) in approval, 'Staged scheduler must match the compiled app approval'
    env.update(QT_QPA_PLATFORM='offscreen', QT_SCALE_FACTOR='1',
        LD_LIBRARY_PATH=':'.join(str(v) for v in (private / 'lib', private / 'qt/lib', candidate / 'dependencies/libtorrent/usr/lib/x86_64-linux-gnu')),
        QT_PLUGIN_PATH=str(private / 'qt/plugins'), QT_QPA_PLATFORM_PLUGIN_PATH=str(private / 'qt/plugins/platforms'))
    for name in ('BiglyBTC', 'BiglyBTC-headless'):
        run('loader-' + name, ['ldd', str(private / 'bin' / name)], 30)
        loader = (out / ('loader-' + name + '.log')).read_text()
        assert 'not found' not in loader and '/opt/Qt' not in loader and '/tmp/' not in loader
        assert str(private / 'lib/libtorrent-rasterbar.so.2.0') in loader
        run('rpath-' + name, ['readelf', '--dynamic', str(private / 'bin' / name)], 30)
        dynamic = (out / ('rpath-' + name + '.log')).read_text()
        assert '/opt/Qt' not in dynamic and '/tmp/' not in dynamic and str(build) not in dynamic
    run('loader-test', ['ldd', str(build / 'biglybtc-preview-smoke')], 30)
    loader = (out / 'loader-test.log').read_text()
    assert str(private / 'qt/lib/libQt6Core.so.6') in loader and str(private / 'lib/libtorrent-rasterbar.so.2.0') in loader
    assert 'not found' not in loader
    for line in loader.splitlines():
        if '/opt/Qt' in line:
            assert line.lstrip().startswith('libQt6Test.so.6 => '), line
    sample = Path(tempfile.mkdtemp(prefix='biglybtc-preview-', dir='/tmp')); sample.rmdir()
    run('prepare-generated-profile', ['python3', str(NATIVE / 'preview/native/packaging/preview.py'), '--prepare', str(sample)], 30)
    for key in ('XDG_CONFIG_HOME', 'XDG_CACHE_HOME', 'XDG_RUNTIME_DIR'):
        directory = sample / key.lower(); directory.mkdir(mode=0o700); env[key] = str(directory)
    env.update(BIGLYBTC_PREVIEW_PROFILE=str(sample / 'profile'), BIGLYBTC_PREVIEW_CAPTURE=str(sample / 'classic-preview.png'))
    before_usage = {str(v.relative_to(sample)): sha(v) for v in sample.rglob('*') if v.is_file()}
    # No --help exists. No args return2+Usage BEFORE profile path lookup,
    # QApplication, Bridge, engine or normal network initialization.
    run('normal-launcher-usage', [str(normal_launcher)], 30, expected_exit=2)
    assert 'Usage: BiglyBTC --profile ABSOLUTE_PROFILE' in (out / 'normal-launcher-usage.log').read_text()
    assert {str(v.relative_to(sample)): sha(v) for v in sample.rglob('*') if v.is_file()} == before_usage
    run('smoke-private-runtime', [str(build / 'biglybtc-preview-smoke'), '-o', '-,txt'], 300)
    total = re.findall(r'Totals:\s+(\d+) passed,\s+0 failed,\s+0 skipped', (out / 'smoke-private-runtime.log').read_text())
    assert len(total) == 1 and int(total[0]) >= 13
    assert set(plan['retained_capture_names']).issubset({p.name for p in sample.glob('*.png')}), 'An earlier preview capture is missing'
    report = out / 'workflow-tests.xml'
    run('workflow-private-runtime', ['ctest', '--test-dir', str(build), '-R', '^(' + '|'.join(re.escape(t) for t in plan['tests']) + ')$',
        '--no-tests=error', '--output-on-failure', '--output-junit', str(report)], 900)
    tree = ET.parse(report)
    assert {t.attrib['name'] for t in tree.iter('testcase')} == set(plan['tests'])
    assert not list(tree.iter('failure')) and not list(tree.iter('error')) and not list(tree.iter('skipped'))
    source(); assert manifest(candidate)[0] == digest and manifest(NATIVE / 'dist')[0] == old
    record = {'status': 'PRIVATE_RUNTIME_QUALIFIED_NOT_PROMOTED', 'native_source': a.source,
        'source_map_sha256': sha(out / 'source-map.json'), 'candidate': str(candidate), 'build': str(build),
        'manifest_sha256': digest, 'manifest_files': len(bundle['files_sha256']), 'previous_manifest_sha256': old,
        'tests': plan['tests'], 'suite_count': len(plan['tests']), 'smoke_qt_rows': int(total[0]),
        'captures': {p.name: sha(p) for p in sample.glob('*.png')}, 'capture_scale': 1,
        'sample_root': str(sample), 'provider_sha256': DSO_SHA, 'qt_version': qt['qt_version'],
        'normal_launcher_usage_only': True, 'normal_launcher_profile_opened': False,
        'original_profile_or_process_access': False, 'system_installation': False, 'promoted': False}
    (out / 'result.json').write_text(json.dumps(record, indent=2) + '\n')
    print(json.dumps({'source': a.source, 'manifest': digest, 'suites': len(plan['tests']),
        'smoke_qt_rows': int(total[0]), 'captures': len(record['captures']), 'promoted': False}), flush=True)

if __name__ == '__main__':
    main()
