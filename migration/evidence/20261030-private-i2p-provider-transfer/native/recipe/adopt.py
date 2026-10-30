"""Read-only final-source/cache guards. Reuses accepted snapshot/shipping helpers."""
import hashlib,importlib.util,pathlib
W=pathlib.Path(__file__).resolve().parent
PREVIOUS=W.parent/'payload-fixture-layout'
assert hashlib.sha256((PREVIOUS/'adopt.py').read_bytes()).hexdigest()=='86247abd5262e09562d53f951c2e9e61375bdad2109c9ecbcecefd0ada10a5f3'
spec=importlib.util.spec_from_file_location('previous_source_helpers',PREVIOUS/'adopt.py');p=importlib.util.module_from_spec(spec);spec.loader.exec_module(p)
PARENT=p.PARENT;R=p.R;N=p.N;BINDING=p.BINDING;sha=p.sha;load=p.load;write=p.write;ref=p.ref;protected=p.protected;shipping=p.shipping;snapshot=p.snapshot
SOURCE='9f46cf05d133624e48c6d60d45d2c503a6141ff9'
ADOPTION=pathlib.Path('/home/yurii/Java/BiglyBT/build/i2p-provider-attachment-20261029/implementation/amendments/outgoing-unspecified-port/provider-test-lifetime-pin-adoption.json')
EXPECTED_ORIGINAL={'head': 'cb8c6cb9bb97916cbc7e80338a017200ffc3e71f', 'index_sha256': 'ac45601f3bed2b3a3d2ac91d715e4b052108a5b7478de62bb67f165ce0de30d4'}
def pins():
 cap=load(W/'manifest.json')
 for name,pin in cap['artifacts_sha256'].items():assert sha(W/name)==pin,name
 for name,pin in cap['inputs_sha256'].items():assert sha(name)==pin,name
 return cap

def source_guard(source,adoption):
 pins();assert source==SOURCE==adoption['source_commit'] and adoption==load(ADOPTION)
 assert adoption['optional_provider_source_capsule_sha256']=='9d264115b5964f2ed542d193697b77263f974f0d4c3f929e829bd1c4c1398671'
 assert sha(N/adoption['path'])==adoption['after_sha256']
 value=snapshot(source);assert value['files_sha256']==load(W/'source-map.json')
 return value

def cache_origin(cache,lane,approved,values):
 origin=PREVIOUS/'runs'/(lane+'-r3');old=load(origin/'result.json');cv=values(cache)
 assert old['source_commit']=='8e9e248eea56f345f1bef169ea0bfcf7184cc7c3' and old['lane']==lane
 assert old['status']==('PASS_DEFAULT_I2P_ATTACHMENT_CONTROLS' if lane=='default' else 'PASS_PRIVATE_I2P_OWNER_AFFECTED_COHORT')
 assert pathlib.Path(old['cache'])==cache and cache.is_dir() and not cache.is_symlink()
 assert pathlib.Path(cv['CMAKE_HOME_DIRECTORY']).resolve()==N/'preview/native' and cv['CMAKE_GENERATOR']=='Ninja'
 compiler=pathlib.Path('/usr/bin/clang++-24' if lane=='clang' else '/usr/bin/g++-16').resolve()
 assert pathlib.Path(cv['CMAKE_CXX_COMPILER']).resolve()==compiler
 tools=load(origin/'tool-inputs.json');assert sha(compiler)==tools[str(compiler)]['sha256']
 assert sha(cv['CMAKE_MAKE_PROGRAM'])==tools[str(pathlib.Path(cv['CMAKE_MAKE_PROGRAM']).resolve())]['sha256']
 assert (cv['BIGLYBTC_PRIVATE_I2P_PROVIDER_OWNER']=='ON')==(lane!='default')
 installs=[pathlib.Path(old['provider']['install_root']),pathlib.Path(approved['install_root'])] if lane!='default' else [pathlib.Path(old['old_shipping_provider']['install_root'])]
 assert pathlib.Path(cv['LibtorrentRasterbar_DIR']).resolve() in [x/'lib/cmake/LibtorrentRasterbar' for x in installs]
 assert cv['CMAKE_BUILD_TYPE']==('RelWithDebInfo' if lane=='san' else 'Release')
 if lane=='san':assert cv['CMAKE_CXX_FLAGS']=='-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all' and cv['CMAKE_EXE_LINKER_FLAGS']=='-fsanitize=address,undefined'
 return {'historical_result':ref(origin/'result.json'),'cache_sha256_before':sha(cache/'CMakeCache.txt'),'values':cv,'old_tests_credited':False}
