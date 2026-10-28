#!/usr/bin/env python3
"""Assemble completion source only; future Java compilation requires --build."""
import argparse
import json
import pathlib
import re
import shutil
import subprocess
import tempfile

from session_source_scan import balanced, class_audit, mask_java, sha

ROOT = pathlib.Path(__file__).resolve().parents[2]
HERE = ROOT / 'migration/fixtures/java'
EVIDENCE = ROOT / 'migration/evidence/P03-05-persistent-session'
AUDIT = EVIDENCE
DRAFT = EVIDENCE / 'phase2-source-draft'
PLANNED = DRAFT / 'planned'
FIXTURES = ROOT / 'migration/fixtures/session-completion'
INSPECTION_SHA256 = 'f2423932d7fd4b3b7fd55f055cea799141f051fe0b72612f1bbef36911e2abe5'
SOURCE_PLAN_SHA256 = '7bc4b12e18eff20ab2c25ead67dfda658b93ccb8a50f7f36e180e2febc4a07d7'
STABLE_HARNESS_SHA256 = 'e9dca56d934a945313012d426425fe97ff3af12b21f3cc352b529b987d6f3f47'
COMMIT = 'cb8c6cb9bb97916cbc7e80338a017200ffc3e71f'
AUDIT_SHA256 = '76358854aae101de2ea4d9196d5e664c6bc4b5263a418d2ecf5b8ba461e4a17e'
BASE = 'core/src/com/biglybt/'
JAVA = pathlib.Path('/usr/lib/jvm/java-17-openjdk-amd64/bin')
EXTRA = [BASE + name for name in ['core/config/ConfigKeys.java', 'core/disk/DiskManager.java',
    'core/peer/impl/transport/PEPeerTransportProtocol.java']]


def files(folder):
    return {p.relative_to(folder).as_posix(): sha(p.read_bytes()) for p in sorted(folder.rglob('*')) if p.is_file()}


def original(path):
    return subprocess.check_output(['git', 'show', COMMIT + ':' + path], cwd=ROOT)


def selected(source, owner, name):
    found = [m for m in class_audit(source, owner)['methods'] if m['name'] == name]
    if len(found) != 1:
        raise ValueError(('Expected one exact original method', owner, name, len(found)))
    return found[0]


def declaration(source, type_name, name):
    found = re.findall(r'(?:public|private)\s+static\s+final\s+' + type_name + r'\s+' + name + r'\s*=\s*[^;]+;', source)
    if len(found) != 1:
        raise ValueError(('Expected one original constant', name))
    return found[0]


def material():
    raw = (AUDIT / 'source-audit.json').read_bytes()
    if sha(raw) != AUDIT_SHA256:
        raise ValueError('Changed persistent-session source audit')
    audit = json.loads(raw)
    sources = {}
    for name, record in audit['BiglyBT_files'].items():
        data = (AUDIT / 'pinned-source/BiglyBT' / name).read_bytes()
        if sha(data) != record['sha256'] or data != original(name):
            raise ValueError(('Changed original source', name))
        sources[name] = data
    for name in EXTRA:
        data = (DRAFT / 'pinned-source' / name).read_bytes()
        if data != original(name):
            raise ValueError(('Changed additional original source', name))
        sources[name] = data
    controller_path = BASE + 'core/download/impl/DownloadManagerController.java'
    peer_path = BASE + 'core/peer/impl/control/PEPeerControlImpl.java'
    methods = {}
    parts = {}
    for slot, path, owner, name in [
        ('CONTROLLER_SEED', controller_path, 'DownloadManagerController', 'setStateSeeding'),
        ('FORCE_PREDICATE', controller_path, 'DownloadManagerController', 'isForceStart'),
        ('CHECK_FINISHED', peer_path, 'PEPeerControlImpl', 'checkFinished'),
        ('SNUBBED_SETTER', peer_path, 'PEPeerControlImpl', 'setNbPeersSnubbed')]:
        method = selected(sources[path].decode(), owner, name)
        methods[owner + '.' + name] = {'source': path, **method}
        parts[slot] = method['signature'] + '\n' + method['body']
    states = sources[BASE + 'core/download/DownloadManager.java'].decode()
    parts['STATE_CONSTANTS'] = '\n'.join(declaration(states, 'int', key) for key in ['STATE_FINISHING', 'STATE_SEEDING'])
    keys = sources[BASE + 'core/config/ConfigKeys.java'].decode()
    parts['CONFIG_KEYS'] = '\n'.join(declaration(keys, 'String', key) for key in
        ['BCFG_CHECK_PIECES_ON_COMPLETION', 'BCFG_CHECK_PIECES_ON_COMPLETION_BEFORE_MOVE'])
    parts['CHECK_CONSTANT'] = declaration(sources[peer_path].decode(), 'int', 'CHECK_REASON_COMPLETE')
    disk = sources[BASE + 'core/disk/DiskManager.java'].decode()
    masked = mask_java(disk)
    found = re.search(r'public\s+interface\s+DownloadEndedProgress\s*\{', masked)
    opening = found.end() - 1
    parts['PROGRESS_INTERFACE'] = disk[opening:balanced(masked, opening, '{', '}') + 1]
    source = (HERE / 'CompletionOracle.java.in').read_text()
    for name, part in parts.items():
        if source.count('{{' + name + '}}') != 1:
            raise ValueError(('Template slot', name))
        source = source.replace('{{' + name + '}}', part)
    if '{{' in source:
        raise ValueError('Unfilled source slot')
    for qualified, method in methods.items():
        owner, name = qualified.split('.')
        generated = selected(source, owner, name)
        if generated['body'] != method['body'] or generated['signature'] != method['signature']:
            raise ValueError(('Original body/declaration changed', qualified))
    validate_material(source, sources, parts, methods)
    return source, sources, parts, methods


def context(sources):
    specs = {
        'DownloadManagerController': ('core/download/impl/DownloadManagerController.java', {'setState', 'setStateFinishing', 'setForceStart'}),
        'DownloadManagerImpl': ('core/download/impl/DownloadManagerImpl.java', {'downloadEnded', 'setAssumedComplete', 'informDownloadEnded'}),
        'DiskManagerImpl': ('core/disk/impl/DiskManagerImpl.java', {'getRemainingExcludingDND', 'saveResumeData', 'downloadEnded'}),
        'RDResumeHandler': ('core/disk/impl/resume/RDResumeHandler.java', {'saveResumeData'}),
        'PEPeerControlImpl': ('core/peer/impl/control/PEPeerControlImpl.java', {'checkCompletionState', 'schedule'}),
        'PEPeerTransportProtocol': ('core/peer/impl/transport/PEPeerTransportProtocol.java', {'setSnubbed'}),
    }
    result = {}
    for owner, (suffix, names) in specs.items():
        path = BASE + suffix
        methods = [m for m in class_audit(sources[path].decode(), owner)['methods'] if m['name'] in names]
        if names - {m['name'] for m in methods}:
            raise ValueError(('Missing context-only method', owner))
        result[owner] = {'source': path, 'executed_in_helper': False, 'methods': methods}
    defaults_source = sources[BASE + 'core/config/impl/ConfigurationDefaults.java'].decode()
    model = sources[BASE + 'plugin/startstoprules/defaultplugin/StartStopConfigModel.java'].decode()
    snippets = {}
    for name in ['ZERO', 'ONE', 'FALSE', 'TRUE']:
        snippets[name] = re.search(r'private static final Long\s+' + name + r'\s*=\s*[^;]+;', defaults_source).group()
    for name, value in [('Check Pieces on Completion', 'TRUE'), ('Check Pieces on Completion Before Move', 'FALSE')]:
        snippets[name] = re.search(r'def\.put\("' + name + r'"\s*,\s*' + value + r'\s*\);', defaults_source).group()
    snippets['RetainForceStartWhenComplete'] = re.search(r'model\.addBooleanParameter2\("StartStopManager_bRetainForceStartWhenComplete",\s*"[^"]+",\s*false\);', model).group()
    result['source_defaults'] = {'executed_in_helper': False, 'exact_snippets': snippets,
        'sha256': {k: sha(v.encode()) for k, v in snippets.items()},
        'fact': 'Completion recheck true, recheck-before-move false, retain-force UI default false. All helper config values are explicit supplied inputs.'}
    return result


def inputs():
    source_plan()
    paths = [AUDIT / 'source-audit.json', HERE / 'CompletionOracle.java.in',
        ROOT / 'migration/tools/session_source_scan.py',
        ROOT / 'migration/tools/session_completion_oracle.py',
        ROOT / 'migration/tools/session_completion_fixtures.py',
        DRAFT / 'README.md', DRAFT / 'inspection.json', DRAFT / 'ReferenceFence.java',
        HERE / 'ReferenceFence.java', ROOT / 'migration/evidence/P00-04/reference-fence.json']
    paths += sorted((AUDIT / 'pinned-source/BiglyBT').rglob('*.java'))
    paths += sorted((DRAFT / 'pinned-source').rglob('*.java'))
    paths += sorted(PLANNED.glob('*'))

    return {str(p): sha(p.read_bytes()) for p in paths}


def toolchain():
    return {str(JAVA / name): sha((JAVA / name).read_bytes()) for name in ['java', 'javac']}


def verify_fence():
    fence = HERE / 'ReferenceFence.java'
    guard = json.loads((ROOT / 'migration/evidence/P00-04/reference-fence.json').read_text())
    if (DRAFT / 'ReferenceFence.java').read_bytes() != fence.read_bytes() or sha(fence.read_bytes()) != guard['source_sha256']['ReferenceFence.java']:
        raise ValueError('Changed unchanged ReferenceFence')


def command(work):
    return ['nice', '-n', '5', str(JAVA / 'java'), '-XX:ActiveProcessorCount=2', '-Xmx256m',
        '-Dfile.encoding=UTF-8', '-Djava.security.manager=ReferenceFence',
        '-Dbiglybtc.test.root=' + str(work), '-Duser.home=' + str(work),
        '-Dazureus.config.path=' + str(work / 'profile'), '-cp', str(work / 'classes'), 'CompletionOracle']


def inspect():
    verify_fence()
    source, sources, parts, methods = material()
    record = {'status': 'phase2 source-only uncompiled/unexecuted draft', 'commit': COMMIT,
        'source_sha256': {k: sha(v) for k, v in sources.items()}, 'harness_sha256': sha(source.encode()),
        'exact_fragment_sha256': {k: sha(v.encode()) for k, v in parts.items()},
        'original_methods': methods, 'context_only_unexecuted': context(sources),
        'subject_methods': ['DownloadManagerController.setStateSeeding', 'PEPeerControlImpl.checkFinished'],
        'support_methods': ['DownloadManagerController.isForceStart', 'PEPeerControlImpl.setNbPeersSnubbed'],
        'core_on_classpath': False, 'source_JVM_native_execution': False, 'native_equivalence_assumed': False}
    return record


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
    if {k: v['body_sha256'] for k, v in baseline['original_methods'].items()} != {k: v['body_sha256'] for k, v in methods.items()}:
        raise ValueError('Relocation changed original method bodies')


def compile_command(work):
    src, classes = work / 'source', work / 'classes'
    return ['nice', '-n', '5', str(JAVA / 'javac'), '-J-XX:ActiveProcessorCount=2', '-J-Xmx256m',
        '--release', '8', '-encoding', 'UTF-8', '-classpath', str(classes), '-sourcepath', str(src),
        '-d', str(classes), str(src / 'CompletionOracle.java'), str(src / 'ReferenceFence.java')]


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
    if work.parent != pathlib.Path('/tmp') or not work.name.startswith('biglybtc-completion-oracle-') or work.resolve() != work or work.is_symlink():
        raise ValueError('Unexpected exact owned reference root')
    if reference['persistent_work'] != str(persistent_path(work)) or reference['persistent_compile_attempt'] != str(persistent_path(work)):
        raise ValueError('Unexpected persistent reference root')
    if reference['upstream_commit'] != COMMIT or reference['source_audit_sha256'] != AUDIT_SHA256:
        raise ValueError('Changed original source baseline')
    if reference['source_sha256'] != {k: sha(v) for k, v in sources.items()} or reference['harness_sha256'] != STABLE_HARNESS_SHA256:
        raise ValueError('Changed original sources/helper')
    if reference['exact_fragment_sha256'] != {k: sha(v.encode()) for k, v in parts.items()}:
        raise ValueError('Changed original body or fragment')
    expected = {k: v['body_sha256'] for k, v in methods.items()}
    if reference['original_method_sha256'] != expected or reference['adapted_method_sha256'] != expected:
        raise ValueError('Changed original methods')
    if reference['tool_inputs_sha256'] != inputs() or reference['java_launcher_compiler_sha256'] != toolchain():
        raise ValueError('Changed tools/planned commands/toolchain')
    if (work / 'source/CompletionOracle.java').read_text() != source or (work / 'source/ReferenceFence.java').read_bytes() != (HERE / 'ReferenceFence.java').read_bytes():
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
    if not {'CompletionOracle.class', 'ReferenceFence.class'}.issubset(actual) or any(
            re.fullmatch(r'(?:ReferenceFence|CompletionOracle(?:\$[A-Za-z0-9_$]+)?)\.class', name) is None for name in actual):
        raise ValueError('Foreign or missing helper class')
    if reference['class_provenance'] != 'First actual phase2 helper class map; no compiled phase2 predecessor exists':
        raise ValueError('Unexpected phase2 class provenance')
    if reference['core_on_classpath'] or reference['command'] != command(work) or reference['compile_command'] != compile_command(work):
        raise ValueError('Unexpected helper command or classpath')
    return reference


def build():
    # Explicit action only. Java compilation never launches the source JVM.
    destination = ROOT / 'build/recovery/generated-references'
    destination.mkdir(parents=True, exist_ok=True)
    work = pathlib.Path(tempfile.mkdtemp(prefix='biglybtc-completion-oracle-', dir='/tmp'))
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
        (src / 'CompletionOracle.java').write_text(source)
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
            'original_method_sha256': {k: v['body_sha256'] for k, v in methods.items()}, 'adapted_method_sha256': {k: v['body_sha256'] for k, v in methods.items()},
            'harness_sha256': sha(source.encode()), 'helper_class_sha256': files(classes),
            'tool_inputs_sha256': pinned, 'java_launcher_compiler_sha256': state['java_launcher_compiler_sha256'],
            'core_on_classpath': False, 'compile_command': state['compile_command'], 'command': command(work),
            "class_provenance": "First actual phase2 helper class map; no compiled phase2 predecessor exists",
            'scope': 'Two exact completion bodies with scalar force predicate/snub setter and constants. Supplied clock/config/disk/adapter/peer intentions only; first actual phase2 class map. No ordinary constructors, flush/state publication/profile/network or native equality.'}
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
        print('Inspected source-only draft', inspect()['harness_sha256'])


if __name__ == '__main__':
    main()
