#!/usr/bin/env python3
"""Run just the unchanged GCC namespace subject with its 1000-second allowance."""
import importlib.util, pathlib, shutil

W = pathlib.Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('corrected_fixture_basis', W.parent / 'affected-followup/amendments/fin-callback-order/run.py')
d = importlib.util.module_from_spec(spec)
spec.loader.exec_module(d)
b, s, base = d.b, d.s, d.base
SOURCE = '3d58f1c6f4bfe32ec4ea86aae748d39a4afdf742'

def main():
    out = W / 'public-recovery-r2'
    out.mkdir(mode=0o700, exist_ok=False)
    result = {'status': 'FAIL', 'scope': 'one unchanged GCC Public subject only', 'source_commit': SOURCE, 'builds_executed': False, 'javac_invocations': 0}
    env = s.environment('gcc')
    for key in ('ASAN_OPTIONS', 'LSAN_OPTIONS', 'UBSAN_OPTIONS', *b.ADMIN_KEYS):
        env.pop(key, None)
    for key in ('HOME', 'XDG_CONFIG_HOME', 'XDG_CACHE_HOME', 'XDG_RUNTIME_DIR'):
        path = out / key.lower()
        path.mkdir(mode=0o700)
        env[key] = str(path)
    runner = s.Runner(out, env)
    try:
        d.pins()
        adoption = s.load(d.ADOPTION)
        protected = b.protected_original()
        before = d.source_guard(SOURCE, adoption)
        s.write(out / 'source-before.json', before)
        target = b.COMMON['azureus-public-bound']
        cache = s.CACHES['gcc']
        image = cache / 'tests' / target
        old = s.load(d.P / 'runs/gcc-r2/images-before.json')[target]
        assert s.images({target: image})[target] == old
        s.write(out / 'images-before.json', {target: old})
        deps = base.dependency_pins({target: image}, env, out)
        s.write(out / 'dependencies-before.json', deps)
        provider = b.provider_pins()
        dist = s.sha(b.N / 'dist/BUILD_MANIFEST.json')
        runner.run('inventory', ['ctest', '--test-dir', cache, '--show-only=json-v1'], timeout=60)
        selected = next(row for row in s.load(out / 'inventory.log')['tests'] if row['name'] == 'azureus-public-bound')
        assert selected['command'] == ['/usr/bin/python3', str(b.PUBLIC_WRAPPER), str(image)]
        assert {row['name']: row['value'] for row in selected['properties']}['TIMEOUT'] == 1000
        original = adoption
        env.update(b.namespace_environment(out, original))
        try:
            with b.subject_phase(out):
                runner.run('tests', ['nice', '-n', '5', 'ctest', '--test-dir', cache, '--parallel', '1', '-R', '^azureus-public-bound$', '--output-on-failure', '--output-junit', out / 'tests.xml'], timeout=1060)
        finally:
            log = cache / 'Testing/Temporary/LastTest.log'
            if log.exists():
                shutil.copyfile(log, out / 'ctest-full.log')
        assert set(base.check_xml(out / 'tests.xml', {'azureus-public-bound': target})) == {'azureus-public-bound'}
        namespace = b.public_pins(original)['namespace_admin']
        proof = b.current_namespace(out, namespace, selected['command'][2:], b.PUBLIC_LAUNCHER)
        assert d.source_guard(SOURCE, adoption) == before and b.protected_original() == protected
        assert s.images({target: image})[target] == old and base.file_pins(deps) == deps and b.provider_pins() == provider
        assert s.sha(b.N / 'dist/BUILD_MANIFEST.json') == dist
        result.update(status='PASS_EXACT_CURRENT_PUBLIC_RECOVERY', image=old, namespace=proof, original_head_index_unchanged=protected, source_unchanged=True, dependencies_unchanged=True, prior_failure_retained=str(d.P / 'runs/gcc-r2/result.json'))
    except BaseException as exc:
        result['failure'] = repr(exc)
    finally:
        result['runner_sha256'] = s.sha(__file__)
        result['commands_sha256'] = s.sha(out / 'commands.json') if (out / 'commands.json').exists() else None
        result['artifacts_sha256'] = {str(path.relative_to(out)): s.sha(path) for path in out.rglob('*') if path.is_file() and path.suffix in ('.json', '.log', '.xml')}
        s.write(out / 'result.json', result)
    print(result['status'], out / 'result.json', flush=True)
    return 0 if result['status'] == 'PASS_EXACT_CURRENT_PUBLIC_RECOVERY' else 1

if __name__ == '__main__':
    raise SystemExit(main())
