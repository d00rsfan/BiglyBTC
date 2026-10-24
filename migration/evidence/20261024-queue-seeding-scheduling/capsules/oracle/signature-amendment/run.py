#!/usr/bin/env python3
"""Unexecuted recipe. Compile only new inert helpers; original cohort stays byte exact."""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
ROOT = Path('/home/yurii/Java/BiglyBT')
NATIVE = ROOT.parent / 'BiglyBTC'
JAVA = Path('/usr/lib/jvm/java-17-openjdk-amd64/bin')
COHORT = Path('/tmp/biglybtc-oracle/classes')
RETENTION = NATIVE / 'build/dependencies/java-oracle-17-20261021/retention.json'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def inventory(root):
    result = {}
    for path in sorted(root.rglob('*')):
        if path.is_symlink():
            raise ValueError('No symlinks in retained cohort')
        if path.is_file():
            result[path.relative_to(root).as_posix()] = sha(path)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--driver', type=Path, required=True, help='Root-built queue_rank_driver, never built here')
    parser.add_argument('--attempt', required=True, help='Fresh immutable run directory name')
    args = parser.parse_args()
    assert args.attempt.startswith('run-') and '/' not in args.attempt
    destination = HERE / args.attempt
    assert not destination.exists() and args.driver.is_file()
    manifest = json.loads((HERE / 'manifest.json').read_text())
    for name, digest in manifest['artifact_sha256'].items():
        assert sha(HERE / name) == digest, name
    retained = json.loads(RETENTION.read_text())
    assert retained['source_commit'] == 'cb8c6cb9bb97916cbc7e80338a017200ffc3e71f'
    assert retained['recompiled'] is False and sha(RETENTION) == manifest['retention_sha256']
    assert inventory(COHORT) == retained['file_sha256']
    work = Path(tempfile.mkdtemp(prefix='biglybtc-queue-oracle-', dir='/tmp'))
    for name in ('classes', 'empty-sourcepath', 'tmp'):
        (work / name).mkdir()
    environment = {key: value for key, value in os.environ.items()
        if key not in ('CLASSPATH', 'JAVA_TOOL_OPTIONS', '_JAVA_OPTIONS', 'JDK_JAVA_OPTIONS', 'LD_PRELOAD')}
    commands = []

    def run(name, argv, fixture=False):
        record = {'name': name, 'argv': list(map(str, argv))}
        commands.append(record)
        (work / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
        with (work / (name + '.stdout')).open('w') as stdout, (work / (name + '.stderr')).open('w') as stderr:
            source = (HERE / 'vectors.tsv').open('rb') if fixture else subprocess.DEVNULL
            try:
                process = subprocess.Popen(record['argv'], cwd=work, env=environment, stdin=source,
                    stdout=stdout, stderr=stderr)
                try:
                    process.wait(timeout=40)
                except subprocess.TimeoutExpired:
                    print('Queue oracle still running: ' + name, flush=True)
                    try:
                        process.wait(timeout=40)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
                        raise
                record['exit_code'] = process.returncode
                (work / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
                assert process.returncode == 0, name
            finally:
                if fixture:
                    source.close()

    try:
        sources = sorted((HERE / 'source').rglob('*.java'))
        assert {path.relative_to(HERE).as_posix() for path in sources} == set(manifest['java_source_allowlist']), 'Exact authored Java source allowlist required'
        run('javac-new-helpers', [JAVA / 'javac', '-J-XX:ActiveProcessorCount=2', '--release', '8',
            '-encoding', 'UTF-8', '-proc:none', '-implicit:none', '-sourcepath', work / 'empty-sourcepath',
            '-cp', COHORT, '-d', work / 'classes', *sources])
        # javac may emit only authored helper classes or their own nested classes;
        # no source is available for implicit original recompilation.
        allowed = {path.relative_to(HERE / 'source').with_suffix('').as_posix() for path in sources}
        for path in (work / 'classes').rglob('*.class'):
            logical = path.relative_to(work / 'classes').with_suffix('').as_posix().split('$', 1)[0]
            assert logical in allowed, logical
        run('original', [JAVA / 'java', '-XX:ActiveProcessorCount=2', '-Xmx128m',
            '-Djava.security.manager=allow', '-Dqueue.oracle.root=' + str(work),
            '-Dqueue.oracle.cohort=' + str(COHORT), '-Djava.io.tmpdir=' + str(work / 'tmp'),
            '-Duser.home=' + str(work / 'forbidden-home'),
            '-Dazureus.config.path=' + str(work / 'forbidden-profile'),
            '-cp', work / 'classes', 'QueueRankLauncher', work / 'classes', COHORT], fixture=True)
        run('native', [args.driver.resolve()], fixture=True)
        from compare import compare
        result = compare(HERE / 'vectors.json', work / 'original.stdout', work / 'native.stdout')
        result.update({'original_baseline': retained['source_commit'], 'cohort_files': len(retained['file_sha256']),
            'retention_sha256': sha(RETENTION), 'native_driver_sha256': sha(args.driver),
            'original_recompiled': False, 'original_profile_or_process_access': False,
            'commands': commands, 'manifest_sha256': sha(HERE / 'manifest.json')})
        assert inventory(COHORT) == retained['file_sha256']
        (work / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
        assert result['status'] == 'PASS', 'scalar mismatches retained in result.json'
        print(json.dumps({key: result[key] for key in ('status', 'original_cases', 'admitted_scalar_comparisons')}), flush=True)
    finally:
        try:
            cohort_ok = inventory(COHORT) == retained['file_sha256']
        except Exception as error:
            cohort_ok = False
            (work / 'cohort-check-error.txt').write_text(repr(error) + '\n')
        shutil.copytree(work, destination)
        print('All attempt data retained at ' + str(destination), flush=True)
        assert cohort_ok, 'Retained original cohort changed; failure data archived'


if __name__ == '__main__':
    main()
