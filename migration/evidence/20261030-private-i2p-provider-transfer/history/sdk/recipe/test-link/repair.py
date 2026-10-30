#!/usr/bin/env python3
"""Root-only r1 link repair; imports the immutable parent only when invoked."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import sys

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
BASE = HERE.parent.parent / 'provider.py'
CANDIDATE = Path('/home/yurii/Java/BiglyBT/build/i2p-provider-attachment-20261029/provider-candidate-r1')


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def load(path):
    return json.loads(Path(path).read_text())


def cache_values(path):
    return {m[1]: m[2] for line in Path(path).read_text().splitlines()
            if (m := re.match(r'^([^#/][^:]*):[^=]+=(.*)$', line))}


def unchanged(manifest):
    for path, pin in manifest['unchanged_files_sha256'].items():
        assert sha(path) == pin, path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('phase', choices=('build', 'record'))
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--attempt', required=True)
    args = parser.parse_args()
    assert args.output == CANDIDATE and args.attempt != 'r1'
    assert re.fullmatch(r'[a-z0-9][a-z0-9-]{0,40}', args.attempt)
    manifest = load(HERE / 'manifest.json')
    for name, pin in manifest['artifacts_sha256'].items():
        assert sha(HERE / name) == pin, name
    for path, pin in manifest['inputs_sha256'].items():
        assert sha(path) == pin, path
    unchanged(manifest)
    spec = importlib.util.spec_from_file_location('unchanged_private_provider', BASE)
    parent = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(parent)
    parent.verify(CANDIDATE)
    hook = HERE / 'test-objects.cmake'
    cache = CANDIDATE / 'build/CMakeCache.txt'
    expected_cache = dict(manifest['initial_cache_values'], CMAKE_PROJECT_INCLUDE=str(hook))
    out = CANDIDATE / 'runs' / args.attempt
    association = {
        'manifest': {'path': str(HERE / 'manifest.json'), 'sha256': sha(HERE / 'manifest.json')},
        'runner_sha256': sha(__file__),
        'parent_recipe_sha256': sha(BASE),
        'failed_r1_result_sha256': sha(CANDIDATE / 'runs/r1/result.json'),
        'provider_source_manifest_sha256': manifest['provider_source_manifest_sha256'],
        'reused_files_sha256': manifest['unchanged_files_sha256'],
        'production_or_sdk_source_changed': False,
        'provider_dso_relinked': False,
        'new_test_link_only': True,
    }
    if args.phase == 'build':
        assert sha(cache) == manifest['initial_cache_sha256']
        assert sha(CANDIDATE / 'build/build.ninja') == manifest['initial_ninja_sha256']
        configure = parent.config

        def amended_config(candidate, source, build, extra):
            command = configure(candidate, source, build, extra)
            if Path(source) == CANDIDATE / 'source':
                command.append('-DCMAKE_PROJECT_INCLUDE=' + str(hook))
            return command

        parent.config = amended_config
        try:
            parent.build(CANDIDATE, args)
            unchanged(manifest)
            assert cache_values(cache) == expected_cache
            result = load(out / 'result.json')
            association['cache_after_sha256'] = sha(cache)
            parent.b.dump(out / 'test-link-amendment.json', association)
            result['test_link_amendment_sha256'] = sha(out / 'test-link-amendment.json')
            result['amended_recipe_sha256'] = sha(__file__)
            parent.b.dump(out / 'result.json', result)
        except BaseException as error:
            if (out / 'result.json').exists():
                result = load(out / 'result.json')
                result.update(status='FAIL', amendment_failure=repr(error))
                parent.b.dump(out / 'result.json', result)
            raise
    else:
        assert cache_values(cache) == expected_cache
        result = load(out / 'result.json')
        actual = load(out / 'test-link-amendment.json')
        assert actual == dict(association, cache_after_sha256=sha(cache))
        assert result['amended_recipe_sha256'] == sha(__file__)
        assert result['test_link_amendment_sha256'] == sha(out / 'test-link-amendment.json')
        parent.record(CANDIDATE, args)
        record_path = CANDIDATE / 'source-build-record.json'
        record = load(record_path)
        record['test_link_amendment'] = actual
        record['evidence_sha256']['runs/' + args.attempt + '/test-link-amendment.json'] = sha(out / 'test-link-amendment.json')
        parent.b.dump(record_path, record)
        proposal_path = CANDIDATE / 'approval-proposal/private-i2p-owner-runtime.json'
        proposal = load(proposal_path)
        proposal['source_build_record_sha256'] = sha(record_path)
        parent.b.dump(proposal_path, proposal)
        print('MEASURED_AMENDED_RECORD_NOT_APPROVED', sha(record_path), flush=True)


if __name__ == '__main__':
    main()
