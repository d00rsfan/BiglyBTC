#!/usr/bin/env python3
"""Assemble original export/pause bodies; Java compilation requires --build."""
import argparse
import hashlib
import json
import pathlib
import re
import shutil
import subprocess
import tempfile

from session_source_scan import balanced, class_audit, mask_java

ROOT = pathlib.Path(__file__).resolve().parents[2]
HERE = ROOT / 'migration/fixtures/java'
EVIDENCE = ROOT / 'migration/evidence/P03-05-persistent-session'
AUDIT = EVIDENCE
DRAFT = EVIDENCE / 'phase1-source-draft'
PLANNED = DRAFT / 'planned'
FIXTURES = ROOT / 'migration/fixtures/session-export-pause'
INSPECTION_SHA256 = 'f11245b9e24d51c147d6dfc9473db08fd85f883a0feb191ab22f2e7cbfec2001'
SOURCE_PLAN_SHA256 = '5b04787519d2e9eb93ca890e18d19dee370aea738f2b1846728f97536558d9df'
STABLE_HARNESS_SHA256 = '8c75ae7e74580221619348d23a7bb867faf1782ab51fb1207124d7b98fe61f3a'
COMMIT = 'cb8c6cb9bb97916cbc7e80338a017200ffc3e71f'
AUDIT_SHA256 = '76358854aae101de2ea4d9196d5e664c6bc4b5263a418d2ecf5b8ba461e4a17e'
JAVA = pathlib.Path('/usr/lib/jvm/java-17-openjdk-amd64/bin')
BASE = 'core/src/com/biglybt/'
GM = BASE + 'core/global/impl/GlobalManagerImpl.java'
EXTRA = [BASE + s for s in ('core/disk/DiskManager.java', 'pif/utils/ByteArrayWrapper.java',
    'core/torrent/impl/TOTorrentImpl.java', 'core/download/DownloadManagerStats.java',
    'core/config/impl/ConfigurationManager.java', 'core/config/COConfigurationManager.java')]
METHODS = {'exportDownloadStateToMapSupport', 'pauseDownload', 'resumeDownload',
    'resumingDownload', 'stopPausedDownload', 'isPaused'}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def files(folder):
    return {p.relative_to(folder).as_posix(): sha(p.read_bytes())
            for p in sorted(folder.rglob('*')) if p.is_file()}


def original(path):
    return subprocess.check_output(['git', 'show', COMMIT + ':' + path], cwd=ROOT)


def fragment(source, kind, name):
    masked = mask_java(source)
    match = re.search(r'\b' + kind + r'\s+' + name + r'\b[^{}]*\{', masked)
    if not match:
        raise ValueError(('Missing original fragment', kind, name))
    start = match.end() - 1
    end = balanced(masked, start, '{', '}') + 1
    return source[start:end]


def declarations(source, names):
    result = []
    for name in names:
        found = re.findall(r'public\s+static\s+final\s+int\s+' + name + r'\s*=\s*[^;]+;', source)
        if len(found) != 1:
            raise ValueError(('Ambiguous original constant', name))
        result.append(found[0])
    return '\n'.join(result)


def material():
    audit_bytes = (AUDIT / 'source-audit.json').read_bytes()
    if sha(audit_bytes) != AUDIT_SHA256:
        raise ValueError('Changed original persistent-session source audit')
    audit = json.loads(audit_bytes)
    sources = {}
    for name, record in audit['BiglyBT_files'].items():
        data = (AUDIT / 'pinned-source/BiglyBT' / name).read_bytes()
        if sha(data) != record['sha256'] or data != original(name):
            raise ValueError(('Changed original source', name))
        sources[name] = data
    for name in EXTRA:
        data = (DRAFT / 'pinned-source' / name).read_bytes()
        if data != original(name):
            raise ValueError(('Changed additional source dependency', name))
        sources[name] = data
    gm = sources[GM].decode()
    methods = [m for m in class_audit(gm, 'GlobalManagerImpl')['methods'] if m['name'] in METHODS]
    if len(methods) != 6 or {m['name'] for m in methods} != METHODS:
        raise ValueError('Expected exactly six original GlobalManager methods')
    original_methods = '\n\n'.join(m['signature'] + '\n' + m['body'] for m in methods)
    constants = sources[BASE + 'core/download/DownloadManager.java'].decode()
    state_names = re.findall(r'public static final int\s+(STATE_\w+)\s*=', constants)
    pause_declaration = re.search(r'private\s+static\s+class\s+PauseState\s*\{', mask_java(gm))
    pause_open = pause_declaration.end() - 1
    pause_end = balanced(mask_java(gm), pause_open, '{', '}') + 1
    parts = {
        'ORIGINAL_METHODS': original_methods,
        'PAUSE_STATE': gm[pause_declaration.start():pause_end],
        'HASHWRAPPER': fragment(sources[BASE + 'core/util/HashWrapper.java'].decode(), 'class', 'HashWrapper'),
        'BYTEWRAPPER': fragment(sources[BASE + 'pif/utils/ByteArrayWrapper.java'].decode(), 'interface', 'ByteArrayWrapper'),
        'DOWNLOAD_CONSTANTS': declarations(constants, state_names + ['ET_NONE']),
        'DISK_CONSTANTS': declarations(sources[BASE + 'core/disk/DiskManager.java'].decode(), ['ET_NONE']),
        'OWNER_CONSTANTS': declarations(sources[BASE + 'core/global/GlobalManager.java'].decode(), ['PS_ANY', 'PS_GENERAL', 'PS_SPEED_LIMIT_HANDLER']),
    }
    source = (HERE / 'ExportPauseOracle.java.in').read_text()
    for name, data in parts.items():
        if source.count('{{' + name + '}}') != 1:
            raise ValueError(('Template slot', name))
        source = source.replace('{{' + name + '}}', data)
    if '{{' in source:
        raise ValueError('Unfilled source slot')
    generated = class_audit(source, 'GlobalManagerImpl')['methods']
    generated = {m['name']: m for m in generated if m['name'] in METHODS}
    for method in methods:
        got = generated[method['name']]
        if method['body'] != got['body'] or method['signature'] != got['signature']:
            raise ValueError(('Original body/signature changed', method['name']))
    for kind, name, key in [('class', 'PauseState', 'PAUSE_STATE'), ('class', 'HashWrapper', 'HASHWRAPPER'),
                             ('interface', 'ByteArrayWrapper', 'BYTEWRAPPER')]:
        expected = fragment(parts[key], kind, name) if key == 'PAUSE_STATE' else parts[key]
        if fragment(source, kind, name) != expected:
            raise ValueError(('Changed exact original body', name))
    validate_material(source, sources, parts, methods)
    return source, sources, parts, methods


def getter_provenance(sources):
    specs = {
        'DownloadManagerImpl': ('core/download/impl/DownloadManagerImpl.java',
            {'getStats', 'getTorrent', 'getAbsoluteSaveLocation', 'isPersistent', 'getTorrentFileName',
             'getState', 'getErrorType', 'getErrorFlags', 'getErrorDetails', 'getAssumedComplete',
             'isForceStart', 'getPosition', 'getMaxUploads', 'getCreationTime', 'isDataAlreadyAllocated',
             'getUserData', 'isPaused', 'stopIt', 'setForceStart', 'saveDownload'}),
        'DownloadManagerStatsImpl': ('core/download/impl/DownloadManagerStatsImpl.java',
            {'getDownloadRateLimitBytesPerSecond', 'getUploadRateLimitBytesPerSecond',
             'getTotalDataBytesReceived', 'getTotalDataBytesSent', 'getDownloadCompletedBytes',
             'getDiscarded', 'getHashFailBytes', 'getSecondsDownloading', 'getSecondsOnlySeeding'}),
        'TOTorrentImpl': ('core/torrent/impl/TOTorrentImpl.java', {'getHash', 'getHashWrapper'}),
        'ConfigurationManager': ('core/config/impl/ConfigurationManager.java', {'getBooleanParameter'}),
        'COConfigurationManager': ('core/config/COConfigurationManager.java', {'getBooleanParameter'}),
    }
    result = {}
    for name, (suffix, selected) in specs.items():
        data = sources[BASE + suffix].decode()
        found = [m for m in class_audit(data, name)['methods'] if m['name'] in selected]
        missing = selected - {m['name'] for m in found}
        if missing:
            raise ValueError(('Missing original getter/effect provenance', name, sorted(missing)))
        result[name] = {'source': BASE + suffix, 'executed_in_this_helper': False, 'methods': found}
    return result


def defaults(sources):
    config = sources[BASE + 'core/config/impl/ConfigurationDefaults.java'].decode()
    model = sources[BASE + 'plugin/startstoprules/defaultplugin/StartStopConfigModel.java'].decode()
    snippets = {}
    for name in ['ZERO', 'FALSE']:
        snippets[name] = re.search(r'private static final Long\s+' + name + r'\s*=\s*[^;]+;', config).group()
    for name in ['Pause Downloads On Exit', 'Resume Downloads On Start']:
        snippets[name] = re.search(r'def\.put\("' + name + r'"\s*,\s*FALSE\s*\);', config).group()
    snippets['RetainForceStartWhenComplete'] = re.search(r'model\.addBooleanParameter2\("StartStopManager_bRetainForceStartWhenComplete",\s*"[^"]+",\s*false\);', model).group()
    return {'exact_source_snippets': snippets,
        'snippet_sha256': {k: sha(v.encode()) for k, v in snippets.items()},
        'source_fact': 'Exit/resume defaults use Long FALSE=ZERO; retain-force is declared with false by the original start/stop configuration model.',
        'executed_in_helper': False,
        'exclusion': 'No configuration singleton, settings write, UI model or force-on-completion policy runs in phase1. Supplied DM/Stats values are test inputs, not asserted original defaults.'}


def inputs():
    source_plan()
    paths = [AUDIT / 'source-audit.json', HERE / 'ExportPauseOracle.java.in',
        ROOT / 'migration/tools/session_source_scan.py',
        ROOT / 'migration/tools/session_export_pause_oracle.py',
        ROOT / 'migration/tools/session_export_pause_fixtures.py',
        DRAFT / 'README.md', DRAFT / 'inspection.json', DRAFT / 'ReferenceFence.java',
        HERE / 'ReferenceFence.java', ROOT / 'migration/evidence/P00-04/reference-fence.json']
    paths += sorted((AUDIT / 'pinned-source/BiglyBT').rglob('*.java'))
    paths += sorted((DRAFT / 'pinned-source').rglob('*.java'))
    paths += sorted(PLANNED.glob('*'))
    paths += [FIXTURES / "manifest.json"]
    return {str(p): sha(p.read_bytes()) for p in paths}


def toolchain():
    return {str(JAVA / name): sha((JAVA / name).read_bytes()) for name in ['java', 'javac']}


def command(work):
    return ['nice', '-n', '5', str(JAVA / 'java'), '-XX:ActiveProcessorCount=2', '-Xmx256m',
        '-Dfile.encoding=UTF-8', '-Djava.security.manager=ReferenceFence',
        '-Dbiglybtc.test.root=' + str(work), '-Duser.home=' + str(work),
        '-Dazureus.config.path=' + str(work / 'profile'), '-cp', str(work / 'classes'), 'ExportPauseOracle']


def verify_fence():
    fence = HERE / 'ReferenceFence.java'
    guard = json.loads((ROOT / 'migration/evidence/P00-04/reference-fence.json').read_text())
    if (DRAFT / 'ReferenceFence.java').read_bytes() != fence.read_bytes() or sha(fence.read_bytes()) != guard['source_sha256']['ReferenceFence.java']:
        raise ValueError('Changed unchanged ReferenceFence')


def inspect():
    verify_fence()
    source, sources, parts, methods = material()
    record = {'status': 'source-only assembled/uncompiled/unexecuted', 'commit': COMMIT,
        'source_sha256': {k: sha(v) for k, v in sources.items()},
        'harness_sha256': sha(source.encode()), 'exact_fragment_sha256': {k: sha(v.encode()) for k, v in parts.items()},
        'original_methods': methods, 'getter_and_effect_source_provenance': getter_provenance(sources),
        'original_defaults_source_facts': defaults(sources), 'core_on_classpath': False,
        'source_compiler_JVM_native_execution': False, 'native_equivalence_assumed': False}
    return record


FROZEN_MANIFEST_SHA256 = '6e60e51825488fce6e5717e90a4414c7eec908e383665be7a5f8dd5b0ce72b33'
FROZEN_REFERENCE_SHA256 = 'b2e8e316a6a5cc860f2082686c1639a6077810adbd538d252d75deadc1711ccc'


def frozen_reference():
    raw = (FIXTURES / 'manifest.json').read_bytes()
    if sha(raw) != FROZEN_MANIFEST_SHA256:
        raise ValueError('Changed frozen original phase1 manifest')
    frozen = json.loads(raw)
    ref = frozen['reference']
    if sha((json.dumps(ref, indent=2) + '\n').encode()) != FROZEN_REFERENCE_SHA256:
        raise ValueError('Changed previously compiled source reference')
    if len(ref['helper_class_sha256']) != 17 or ref['harness_sha256'] != STABLE_HARNESS_SHA256:
        raise ValueError('Changed actual frozen seventeen-class baseline')
    for name, group in frozen['groups'].items():
        for rel, digest in group['files_sha256'].items():
            if sha((FIXTURES / name / rel).read_bytes()) != digest:
                raise ValueError(('Changed immutable original observations', name, rel))
    return ref


def class_proof():
    return {'kind': 'byte equality with the seventeen previously frozen actual helper classes',
        'fixture_manifest_sha256': FROZEN_MANIFEST_SHA256,
        'source_reference_sha256': FROZEN_REFERENCE_SHA256,
        'helper_class_sha256': frozen_reference()['helper_class_sha256']}


def source_plan():
    path = PLANNED / 'plan.json'
    if sha(path.read_bytes()) != SOURCE_PLAN_SHA256:
        raise ValueError('Changed historical pre-execution source plan')
    record = json.loads(path.read_text())
    for name, group in record['groups'].items():
        data = (PLANNED / (name + '.txt')).read_bytes()
        if sha(data) != group['sha256'] or len(data.splitlines()) != group['commands']:
            raise ValueError(('Changed historical source commands', name))
    return record


def validate_material(source, sources, parts, methods):
    raw = (DRAFT / 'inspection.json').read_bytes()
    if sha(raw) != INSPECTION_SHA256:
        raise ValueError('Changed historical source inspection')
    baseline = json.loads(raw)
    if sha(source.encode()) != STABLE_HARNESS_SHA256 or baseline['harness_sha256'] != STABLE_HARNESS_SHA256:
        raise ValueError('Relocation changed original generated Java')
    if baseline['source_sha256'] != {k: sha(v) for k, v in sources.items()}:
        raise ValueError('Relocation changed original source pins')
    if baseline['exact_fragment_sha256'] != {k: sha(v.encode()) for k, v in parts.items()}:
        raise ValueError('Relocation changed original fragments')
    if {m['name']: m['body_sha256'] for m in baseline['original_methods']} != {m['name']: m['body_sha256'] for m in methods}:
        raise ValueError('Relocation changed original method bodies')
    frozen_reference()


def compile_command(work):
    src, classes = work / 'source', work / 'classes'
    return ['nice', '-n', '5', str(JAVA / 'javac'), '-J-XX:ActiveProcessorCount=2',
        '--release', '8', '-encoding', 'UTF-8', '-classpath', str(classes), '-sourcepath', str(src),
        '-d', str(classes), str(src / 'ExportPauseOracle.java'), str(src / 'ReferenceFence.java')]


def persistent_path(work):
    return ROOT / 'build/recovery/generated-references' / work.name


def persist_work(work, persistent):
    expected = files(work)
    shutil.copytree(work, persistent)
    if files(persistent) != expected:
        raise ValueError('Persistent generated reference copy changed bytes')
    (persistent / 'persistent-copy.json').write_text(json.dumps({
        'original_work': str(work), 'persistent_work': str(persistent), 'files_sha256': expected,
        'restore_note': 'Restore this exact owned /tmp path and verify every hash before fenced reuse.'
    }, indent=2) + '\n')


def verify(reference):
    verify_fence()
    source, sources, parts, methods = material()
    work = pathlib.Path(reference['work'])
    if work.parent != pathlib.Path('/tmp') or not work.name.startswith('biglybtc-export-pause-oracle-') or work.resolve() != work or work.is_symlink():
        raise ValueError('Unexpected exact owned reference root')
    if reference['persistent_work'] != str(persistent_path(work)) or reference['persistent_compile_attempt'] != str(persistent_path(work)):
        raise ValueError('Unexpected persistent reference root')
    if reference['upstream_commit'] != COMMIT or reference['source_audit_sha256'] != AUDIT_SHA256:
        raise ValueError('Changed original source baseline')
    if reference['source_sha256'] != {k: sha(v) for k, v in sources.items()} or reference['harness_sha256'] != STABLE_HARNESS_SHA256:
        raise ValueError('Changed original sources/helper')
    if reference['exact_fragment_sha256'] != {k: sha(v.encode()) for k, v in parts.items()}:
        raise ValueError('Changed original body or fragment')
    expected = {m['name']: m['body_sha256'] for m in methods}
    if reference['original_method_sha256'] != expected or reference['adapted_method_sha256'] != expected:
        raise ValueError('Changed original methods')
    if reference['tool_inputs_sha256'] != inputs() or reference['java_launcher_compiler_sha256'] != toolchain():
        raise ValueError('Changed tools/planned commands/toolchain')
    if (work / 'source/ExportPauseOracle.java').read_text() != source or (work / 'source/ReferenceFence.java').read_bytes() != (HERE / 'ReferenceFence.java').read_bytes():
        raise ValueError('Changed compiled source')
    for child in ['source', 'classes']:
        path = work / child
        if not path.is_dir() or path.is_symlink() or path.resolve().parent != work:
            raise ValueError('Unexpected helper source/class directory')
    if any(path.is_symlink() for path in (work / 'classes').rglob('*')):
        raise ValueError('Borrowed helper class path')
    actual = files(work / 'classes')
    if actual != reference['helper_class_sha256']:
        raise ValueError('Changed helper class bytes')
    if actual != frozen_reference()['helper_class_sha256'] or reference['stable_class_proof'] != class_proof():
        raise ValueError('Relocated phase1 helper differs from previously frozen actual classes')
    if reference['core_on_classpath'] or reference['command'] != command(work) or reference['compile_command'] != compile_command(work):
        raise ValueError('Unexpected helper command or classpath')
    return reference


def build():
    # Explicit action only. Java compilation never launches the source JVM.
    destination = ROOT / 'build/recovery/generated-references'
    destination.mkdir(parents=True, exist_ok=True)
    work = pathlib.Path(tempfile.mkdtemp(prefix='biglybtc-export-pause-oracle-', dir='/tmp'))
    persistent = persistent_path(work)
    if persistent.exists():
        raise ValueError('Fresh persistent reference root required')
    state = {'status': 'preparing', 'work': str(work), 'persistent_work': str(persistent),
        'compile_command': compile_command(work)}
    try:
        (work / 'status.json').write_text(json.dumps(state, indent=2) + '\n')
        verify_fence()
        source, sources, parts, methods = material()
        src, classes = work / 'source', work / 'classes'
        src.mkdir(); classes.mkdir()
        (src / 'ExportPauseOracle.java').write_text(source)
        shutil.copyfile(HERE / 'ReferenceFence.java', src / 'ReferenceFence.java')
        for name, data in sources.items():
            path = work / 'source-original' / name
            path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(data)
        pinned = inputs()
        for name, digest in pinned.items():
            path = pathlib.Path(name); target = work / 'before-compile' / path.relative_to(ROOT)
            target.parent.mkdir(parents=True, exist_ok=True); shutil.copyfile(path, target)
            if sha(target.read_bytes()) != digest:
                raise ValueError(('Input changed during preparation snapshot', name))
        state.update(status='compiling', tool_inputs_sha256=pinned, java_launcher_compiler_sha256=toolchain())
        (work / 'status.json').write_text(json.dumps(state, indent=2) + '\n')
        with (work / 'javac.log').open('w') as log:
            subprocess.run(state['compile_command'], stdout=log, stderr=subprocess.STDOUT, check=True, timeout=120)
        reference = {'upstream_commit': COMMIT, 'source_audit_sha256': AUDIT_SHA256,
            'work': str(work), 'persistent_work': str(persistent), 'persistent_compile_attempt': str(persistent),
            'source_sha256': {k: sha(v) for k, v in sources.items()},
            'exact_fragment_sha256': {k: sha(v.encode()) for k, v in parts.items()},
            'original_method_sha256': {m['name']: m['body_sha256'] for m in methods}, 'adapted_method_sha256': {m['name']: m['body_sha256'] for m in methods},
            'harness_sha256': sha(source.encode()), 'helper_class_sha256': files(classes),
            'tool_inputs_sha256': pinned, 'java_launcher_compiler_sha256': state['java_launcher_compiler_sha256'],
            'core_on_classpath': False, 'compile_command': state['compile_command'], 'command': command(work),
            "stable_class_proof": class_proof(),
            'scope': 'Exact GlobalManager export/individual pause bodies and fragments. Inert supplied getter/effect intentions only; all seventeen classes must equal the previously frozen actual helper classes. No ordinary managers, actual controller/disk/state/profile/network/settings or native comparison.'}
        (work / 'reference.json').write_text(json.dumps(reference, indent=2) + '\n')
        verify(reference)
        state['status'] = 'compiled-no-JVM-execution'
        return reference
    except BaseException as error:
        state.update(status='failed', failure_type=type(error).__name__, failure=str(error))
        (work / 'failure.json').write_text(json.dumps(state, indent=2) + '\n')
        raise
    finally:
        # Includes material/fence/input preparation, compiler and verification failures.
        (work / 'status.json').write_text(json.dumps(state, indent=2) + '\n')
        persist_work(work, persistent)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', action='store_true')
    args = parser.parse_args()
    if args.build:
        print('Compiled source helper only', str(pathlib.Path(build()['work']) / 'reference.json'))
    else:
        print('Inspected source draft only', inspect()['harness_sha256'])


if __name__ == '__main__':
    main()
