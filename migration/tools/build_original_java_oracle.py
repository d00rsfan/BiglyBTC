#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build an isolated original Core oracle from one immutable Git commit.

No Maven lifecycle or reference working-tree source is modified. The exclusions
match the original java-9-plus/resources profiles. Output is an owned cache,
never a client profile or a distributable/rollback application.
"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import tarfile

BASELINE = 'cb8c6cb9bb97916cbc7e80338a017200ffc3e71f'
EXCLUDED = {
    'core/src/com/biglybt/core/util/spi/AENameServiceDescriptor.java',
    'core/src/META-INF/services/sun.net.spi.nameservice.NameServiceDescriptor',
}
OWNER = 'BiglyBTC original Java Core oracle'


def digest(data):
    return hashlib.sha256(data).hexdigest()


def write_under(root, relative, data):
    path = root / relative
    if not path.resolve().is_relative_to(root):
        raise RuntimeError('Oracle output must remain inside its owned cache')
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-repo', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    parser.add_argument('--commit', default=BASELINE)
    parser.add_argument('--javac', default='javac')
    args = parser.parse_args()
    if not re.fullmatch(r'[0-9a-f]{40}', args.commit):
        parser.error('--commit must be a full immutable SHA-1 object ID')
    source = args.source_repo.resolve()
    out = args.out.resolve()
    if out == Path.home() or out == source or out in source.parents:
        parser.error('Use a dedicated oracle cache directory')
    version = subprocess.check_output(
        [args.javac, '-version'], stderr=subprocess.STDOUT, text=True).strip()
    if not version.startswith('javac 17.'):
        parser.error('The qualified reference toolchain is JDK17; select --javac')
    out.mkdir(parents=True, exist_ok=True)
    marker = out / '.original-java-oracle-owner.json'
    identity = {'owner': OWNER, 'commit': args.commit}
    if marker.exists():
        if json.loads(marker.read_text()) != identity:
            parser.error('Output belongs to a different owner/commit')
    elif any(out.iterdir()):
        parser.error('An existing unmarked output directory is not an oracle cache')
    else:
        marker.write_text(json.dumps(identity, indent=2) + '\n')
    records, java_sources = [], []
    resources = 0
    process = subprocess.Popen(
        ['git', '-C', str(source), 'archive', args.commit, 'core/src'],
        stdout=subprocess.PIPE)
    with tarfile.open(fileobj=process.stdout, mode='r|') as archive:
        for member in archive:
            name = PurePosixPath(member.name)
            if (not member.isfile() or member.name in EXCLUDED
                    or name.is_absolute() or '..' in name.parts
                    or name.parts[:2] != ('core', 'src')):
                continue
            if (name.suffix == '.class' or name.name == '.editorconfig'
                    or name.name.endswith('.NameServiceDescriptor')):
                continue
            data = archive.extractfile(member).read()
            path = write_under(out, Path('pinned-tree') / str(name), data)
            records.append({'path': str(name), 'sha256': digest(data)})
            if name.suffix == '.java':
                java_sources.append(path)
            else:
                write_under(out, Path('classes') / str(name.relative_to('core/src')), data)
                resources += 1
    if process.wait() != 0:
        raise RuntimeError('Cannot read selected immutable original sources')
    jars, dependencies = [], []
    for name in ('apple-extensions.jar', 'commons-cli.jar'):
        data = subprocess.check_output(
            ['git', '-C', str(source), 'show', args.commit + ':core/lib/' + name])
        jars.append(write_under(out, Path('pinned-lib') / name, data))
        dependencies.append({'path': 'core/lib/' + name, 'sha256': digest(data)})
    argument_file = write_under(out, 'all-core.args',
        ('\n'.join('"' + str(p) + '"' for p in sorted(java_sources)) + '\n').encode())
    command = [args.javac, '-J-Xmx1024m', '-source', '8', '-target', '8',
        '-encoding', 'UTF-8', '-cp', ':'.join(map(str, jars)),
        '-sourcepath', str(out / 'pinned-tree/core/src'),
        '-d', str(out / 'classes'), '@' + str(argument_file)]
    log = out / 'core-compile.log'
    with log.open('w') as stream:
        result = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT)
    if result.returncode:
        lines = log.read_text(errors='replace').splitlines()
        for line in [s for s in lines if 'error:' in s][:3]:
            print(line[:420])
        raise RuntimeError('Original Core compilation failed; see owned compile log')
    class_records = [{'path': str(p.relative_to(out / 'classes')),
        'sha256': digest(p.read_bytes())}
        for p in sorted((out / 'classes').rglob('*')) if p.is_file()]
    manifest = {'commit': args.commit, 'compiler': version,
        'java_sources': len(java_sources), 'resources': resources,
        'excluded': sorted(EXCLUDED), 'sources': records,
        'dependencies': dependencies, 'artifacts': class_records}
    (out / 'oracle-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    (out / 'reference-environment.json').write_text(json.dumps({
        'commit': args.commit, 'classpath': ':'.join(map(str, [out / 'classes', *jars])),
        'manifest': str(out / 'oracle-manifest.json')}, indent=2) + '\n')
    print('PINNED_ORIGINAL_CORE PASS commit=' + args.commit
        + ' java_sources=' + str(len(java_sources))
        + ' resources=' + str(resources) + ' compiler=' + version)


if __name__ == '__main__':
    main()
