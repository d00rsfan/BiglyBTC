#!/usr/bin/env python3
"""Actual native-created metadata -> retained original Java decoder, owned files only.

Compiles just the two test helpers, never the original core. Root runs this once
with its batched creator driver; there is no compiler/test execution on staging.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
ROOT = Path('/home/yurii/Java/BiglyBT')
NATIVE = ROOT.parent / 'BiglyBTC'
HERE = Path(__file__).resolve().parent
JAVA = Path('/usr/lib/jvm/java-17-openjdk-amd64/bin')
BASELINE = 'cb8c6cb9bb97916cbc7e80338a017200ffc3e71f'

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def data(length, seed):
    return bytes((i * 37 + seed) % 251 for i in range(length))

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--driver', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    assert args.driver.is_file() and not args.output.exists()
    retained_path = NATIVE / 'build/dependencies/java-oracle-17-20261021/retention.json'
    retained = json.loads(retained_path.read_text())
    cohort = Path('/tmp/biglybtc-oracle/classes')
    assert retained['source_commit'] == BASELINE and not retained['recompiled']
    def inventory():
        result = {}
        for path in cohort.rglob('*'):
            assert not path.is_symlink()
            if path.is_file(): result[path.relative_to(cohort).as_posix()] = sha(path)
        return result
    assert inventory() == retained['file_sha256']
    guard = ROOT / 'migration/fixtures/java/ReferenceFence.java'
    guard_receipt = json.loads((ROOT / 'migration/evidence/P00-04/reference-fence.json').read_text())
    assert guard_receipt['status'] == 'passed'
    assert sha(guard) == guard_receipt['source_sha256']['ReferenceFence.java']
    work = Path(tempfile.mkdtemp(prefix='biglybtc-creator-java-', dir='/tmp'))
    work.chmod(0o700)
    for name in ('classes', 'empty-sourcepath'): (work / name).mkdir()
    commands, observations = [], []
    environment = {k: v for k, v in os.environ.items() if k not in
                   ('CLASSPATH', 'JAVA_TOOL_OPTIONS', '_JAVA_OPTIONS', 'JDK_JAVA_OPTIONS', 'LD_PRELOAD')}
    def run(name, argv):
        record = {'name': name, 'argv': list(map(str, argv))}
        commands.append(record)
        (work / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
        with (work / (name + '.stdout')).open('w') as out, (work / (name + '.stderr')).open('w') as err:
            result = subprocess.run(record['argv'], cwd=work, env=environment,
                                    stdout=out, stderr=err, timeout=90)
        record['exit_code'] = result.returncode
        (work / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
        assert result.returncode == 0, name
        return (work / (name + '.stdout')).read_text()
    try:
        run('javac-helpers', [JAVA / 'javac', '-J-XX:ActiveProcessorCount=2', '--release', '8',
            '-encoding', 'UTF-8', '-proc:none', '-implicit:none', '-sourcepath', work / 'empty-sourcepath',
            '-cp', cohort, '-d', work / 'classes', guard, HERE / 'TorrentCreationOracle.java'])
        sys.path.insert(0, str(ROOT / 'migration/tools'))
        from peer_interop import jvm
        classpath = str(work / 'classes') + ':' + str(cohort)
        cases = [
            ('single', False, 0, True, [('payload-π.bin', data(77003, 7))]),
            ('multi', True, 49152, True, [('01-first.bin', data(17001, 3)),
                ('a-dir/02-שب.bin', data(50003, 9)), ('a-dir/03-empty.bin', b''), ('z-last.bin', data(101, 1))]),
            ('one-file-directory', True, 65536, False, [('only-π.bin', data(33001, 19))]),
        ]
        for name, directory, requested_piece, private, members in cases:
            case = work / name; case.mkdir()
            source = case / ('bundle-π' if directory else members[0][0])
            if directory: source.mkdir()
            for relative, content in members:
                path = source / relative if directory else source
                path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(content)
            originals = {p.relative_to(case).as_posix(): sha(p) for p in case.rglob('*') if p.is_file()}
            output = case / 'created.torrent'
            native = run(name + '-native', [args.driver.resolve(), source, output, requested_piece, int(private)])
            line, = [line.split('\t') for line in native.splitlines() if line.startswith('CREATED\t')]
            assert len(line) == 6
            _, identity, total, count, piece_size, piece_count = line
            stream = b''.join(content for _, content in members)
            piece_size, piece_count = int(piece_size), int(piece_count)
            assert int(total) == len(stream) and int(count) == len(members)
            assert piece_count == (len(stream) + piece_size - 1) // piece_size
            output_hash = sha(output)
            actual = run(name + '-java', [*jvm(work, classpath), 'TorrentCreationOracle', output])
            fields = [line.split('\t') for line in actual.splitlines()]
            def one(key):
                rows = [row[1:] for row in fields if row[0] == key]
                assert len(rows) == 1 and len(rows[0]) == 1, key
                return rows[0][0]
            assert one('HASH') == identity.lower() and one('TYPE') == '1'
            assert one('NAME') == source.name.encode().hex() and int(one('SIZE')) == len(stream)
            assert int(one('PIECE_LENGTH')) == piece_size and int(one('PIECES')) == piece_count
            assert one('PRIVATE') == str(private).lower() and one('COMMENT') == 'Creator – π'.encode().hex()
            assert one('SIMPLE') == str(not directory).lower() and one('CREATION_DATE') == '0'
            assert int(one('FILES')) == len(members)
            expected_pieces = [['PIECE', str(i), hashlib.sha1(stream[offset:offset + piece_size]).hexdigest()]
                               for i, offset in enumerate(range(0, len(stream), piece_size))]
            assert [row for row in fields if row[0] == 'PIECE'] == expected_pieces
            expected_files = [['FILE', str(i), str(len(content)), '/'.join(part.encode().hex() for part in relative.split('/'))]
                              for i, (relative, content) in enumerate(members)]
            assert [row for row in fields if row[0] == 'FILE'] == expected_files
            trackers = [['TRACKER', '0', value.encode().hex()] for value in
                        ['http://127.0.0.1:19123/announce', 'udp://127.0.0.1:19124/announce']]
            trackers += [['TRACKER', '1', 'https://127.0.0.1:19125/announce'.encode().hex()]]
            assert sorted(row for row in fields if row[0] == 'TRACKER') == sorted(trackers)
            assert one('ANNOUNCE') in {row[2] for row in trackers if row[1] == '0'}
            assert 'ORIGINAL_TORRENT_CREATION_PARSE_PASS' in actual
            assert sha(output) == output_hash
            assert originals == {relative: sha(case / relative) for relative in originals}
            observations.append({'case': name, 'info_hash_v1': identity.lower(), 'source_files': len(members),
                                 'source_bytes': len(stream), 'actual_piece_hashes': piece_count,
                                 'torrent_sha256': output_hash})
        assert inventory() == retained['file_sha256']
        result = {'status': 'PASS_ACTUAL_NATIVE_CREATION_ORIGINAL_JAVA_PARSE', 'original_source': BASELINE,
                  'native_driver_sha256': sha(args.driver), 'retention_sha256': sha(retained_path),
                  'original_core_recompiled': False, 'original_profile_access': False,
                  'network_operations': False, 'cases': observations, 'commands': commands,
                  'scope': 'Actual original torrent decoder; exact v1 pieces, paths, Unicode, tiers, private/comment; no transfer or original creator byte-equality claim'}
        (work / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
        print(result['status'])
    finally:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        shutil.copytree(work, args.output)
        print('Retained original parser handoff:', args.output)

if __name__ == '__main__':
    main()
