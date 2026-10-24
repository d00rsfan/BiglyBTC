#!/usr/bin/env python3
"""One batched build/check runner; full output stays on disk."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time

p = argparse.ArgumentParser()
p.add_argument('build', type=Path)
p.add_argument('--case', required=True)
p.add_argument('--jobs', type=int, default=3)
p.add_argument('--focus', nargs='*')
p.add_argument('--tests-only', action='store_true')
p.add_argument('--capture', action='store_true')
p.add_argument('--cmake-option', action='append', default=[])
a = p.parse_args()
root = Path('/home/yurii/Java/BiglyBTC')
work = Path(__file__).resolve().parent
build = (root / a.build).resolve()
cases = {
 'library-commands':'library-commands', 'network-policy':'network-policy',
 'library-action-ui':'library-action-ui', 'managed-download':'managed-download',
 'managed-tracker':'managed-tracker', 'managed-storage':'managed-storage', 'torrent-intake':'torrent-intake',
 'persistent-native-peer-session':'persistent-session',
 'persistent-native-peer-faults':'persistent-session-fault',
 'persistent-native-session-window-worker':'native-session-integration',
 'persistent-native-session-close-faults':'app-close-fault',
 'startup-statistics-worker':'startup-statistics',
 'desktop-model-and-lifecycle':'desktop', 'desktop-files-model':'desktop',
 'owned-engine-activity-window-worker':'engine-activity-integration',
 'overall-accounting-window-worker':'overall-accounting-integration',
 'tracker-statistics-sampler':'tracker-statistics-sampler',
 'file-policy-commands':'file-policy-commands','file-priority-ui':'file-priority-ui', 'torrent-import-ui':'torrent-import-ui',
 'magnet-intake':'magnet-intake', 'metadata-disk-router':'metadata-disk-router',
 'managed-seeding':'managed-seeding', 'managed-magnet':'managed-magnet',
 'managed-library-command-routing':'library-commands-bridge', 'magnet-host':'magnet-host',
 'application-settings':'application-settings','options-dialog':'options-dialog',
 'options-host':'options-host','engine-live-settings':'engine-live-settings',
 'effective-rate-limits':'effective-rate-limits'}
selected = a.focus or ['application-settings','options-dialog','options-host','engine-live-settings','network-policy',
 'managed-download','managed-seeding','managed-library-command-routing','magnet-host','managed-magnet',
 'desktop-model-and-lifecycle','desktop-files-model','startup-statistics-worker',
 'owned-engine-activity-window-worker','overall-accounting-window-worker','effective-rate-limits',
 'torrent-import-ui','library-action-ui']
assert 1 <= a.jobs <= 3 and all(c in cases for c in selected)
environment = os.environ.copy()
environment.update(QT_QPA_PLATFORM='offscreen', ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
                   UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
if a.capture:
    capture = work / (a.case + '-captures')
    capture.mkdir(mode=0o700)
    environment['BIGLYBTC_OPTIONS_CAPTURE_DIRECTORY'] = str(capture)
    environment['BIGLYBTC_COUNTRY_CAPTURE_DIRECTORY'] = str(capture)
    environment['BIGLYBTC_AZUREUS_OPERATION_CAPTURE_DIRECTORY'] = str(capture)
    environment['BIGLYBTC_DDB_CAPTURE_DIRECTORY'] = str(capture)
    environment['BIGLYBTC_VIVALDI_CAPTURE_DIRECTORY'] = str(capture)
    environment['BIGLYBTC_CONNECTIONS_CAPTURE_DIRECTORY'] = str(capture)
    environment['BIGLYBTC_PROTOCOL_CAPTURE_DIRECTORY'] = str(capture)
    environment['BIGLYBTC_TRACKER_HISTORY_CAPTURE_DIRECTORY'] = str(capture)
receipt = {'case':a.case,'build':str(build),'tests':selected,'commands':[]}
source_manifest = work / (a.case + '-source.json')
source_files = {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted((root/'preview/native').rglob('*')) if p.is_file()}
source_manifest.write_text(json.dumps(source_files, indent=2) + '\n')
receipt['source_manifest_sha256'] = hashlib.sha256(source_manifest.read_bytes()).hexdigest()
def save():
    (work / (a.case + '-receipt.json')).write_text(json.dumps(receipt, indent=2) + '\n')
def run(kind, command):
    log = work / (a.case + '-' + kind + '.txt')
    start = time.monotonic()
    with log.open('w') as out:
        result = subprocess.Popen(command, cwd=root, env=environment, stdout=out, stderr=subprocess.STDOUT)
        while True:
            try:
                result.wait(timeout=45)
                break
            except subprocess.TimeoutExpired:
                print(kind + ' running; full output remains in ' + log.name, flush=True)
    receipt['commands'].append({'command':command,'exit':result.returncode,
        'seconds':round(time.monotonic()-start,2),'log':log.name})
    save()
    if result.returncode:
        lines = log.read_text(errors='replace').splitlines()
        interesting = [line for line in lines if 'error:' in line or 'FAILED:' in line or 'FAIL!' in line]
        print(kind + ' failed: ' + '\n'.join((interesting[:12] or lines[-15:])), flush=True)
        raise SystemExit(result.returncode)
    print(kind + ' passed', flush=True)

if not a.tests_only:
    prefix = ';'.join(str(root / 'build/dependencies' / x) for x in
        ['source-qualified-2.0.12/install','sysroot/usr','country/sysroot/usr']) + ';/opt/Qt/6.12.0/gcc_64'
    run('configure', ['nice','-n','5','cmake','-S','preview/native','-B',str(build),
        '-DCMAKE_PREFIX_PATH='+prefix,
        '-DBIGLYBTC_COUNTRY_MMDB_FIXTURE='+str(root / 'build/dependencies/country/GeoIP2-Country-Test.mmdb'), *a.cmake_option])
    targets = ['biglybtc-'+cases[c]+'-tests' for c in selected]
    targets += ['BiglyBTC','BiglyBTC-headless','biglybtc-preview-smoke','biglybtc-settings-roundtrip-driver']
    keep_going = ['--','-k','0'] if 'CMAKE_GENERATOR:INTERNAL=Ninja' in (build/'CMakeCache.txt').read_text() else []
    run('build', ['nice','-n','5','cmake','--build',str(build),'--parallel',str(a.jobs),'--target',*targets,*keep_going])
registered = json.loads(subprocess.check_output(['ctest','--test-dir',str(build),'--show-only=json-v1'], text=True))
missing = set(selected) - {t['name'] for t in registered['tests']}
if missing:
    raise SystemExit('Unregistered selected tests: '+repr(sorted(missing)))
run('tests', ['nice','-n','5','ctest','--test-dir',str(build),'-R',
    '^('+'|'.join(re.escape(c) for c in selected)+')$','--no-tests=error','--output-on-failure','--verbose',
    '--output-junit',str(work / (a.case+'.xml'))])
print(str(len(selected)) + ' selected suites completed; receipt '+a.case+'-receipt.json', flush=True)
