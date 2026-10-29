#!/usr/bin/env python3
"""Focused profile-I/O and I2CP tracker batch; GCC full products, three-lane affected tests."""
import argparse,importlib.util,pathlib,re,shutil,xml.etree.ElementTree as ET
R=pathlib.Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';H=pathlib.Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('support',R/'build/mainline-scrape-20261028/recipe_support_followup.py');s=importlib.util.module_from_spec(spec);spec.loader.exec_module(s)
SUITES={'azureus-profile-io': 'biglybtc-azureus-profile-io-tests', 'azureus-owned-service': 'biglybtc-azureus-owned-service-tests', 'azureus-database-runtime': 'biglybtc-azureus-database-runtime-tests', 'profile-lease-retention': 'biglybtc-profile-lease-retention-tests', 'compatible-config-first-create': 'biglybtc-config-create-tests', 'native-torrent-state-writer-guards': 'biglybtc-torrent-state-driver', 'i2cp-initial-channel': 'biglybtc-i2cp-initial-channel-tests', 'pure-i2cp-datagram-messages': 'biglybtc-i2cp-datagram-tests', 'i2cp-lease-backed-datagram-channel': 'biglybtc-i2cp-datagram-channel-tests', 'i2cp-bounded-datagram-receiver': 'biglybtc-i2cp-receive-channel-tests', 'pure-i2cp-tracker-objects': 'biglybtc-i2cp-tracker-codec-tests', 'i2cp-explicit-tracker-channel': 'biglybtc-i2cp-tracker-channel-tests', 'classic-preferences-profile-save': 'biglybtc-classic-preferences-view-tests', 'classic-preferences-profile-first-save': 'biglybtc-classic-preferences-create-view-tests', 'local-rating-host-ui': 'biglybtc-local-rating-tests', 'local-rating-wiring': 'biglybtc-local-rating-wiring-tests', 'mainline-single-udp': 'biglybtc-mainline-single-udp-tests'}
GCC_ONLY=['classic-preferences-profile-save', 'classic-preferences-profile-first-save', 'local-rating-host-ui', 'local-rating-wiring', 'mainline-single-udp']
def suites_for_lane(lane):return SUITES if lane=='gcc' else {k:v for k,v in SUITES.items() if k not in GCC_ONLY}
PRODUCTS={'BiglyBTC':'app/BiglyBTC','BiglyBTC-headless':'app/BiglyBTC-headless','biglybtc-preview-smoke':'biglybtc-preview-smoke'}
def main():
 p=argparse.ArgumentParser();p.add_argument('lane',choices=['gcc','clang','san']);p.add_argument('--attempt',required=True);a=p.parse_args();assert re.fullmatch('[A-Za-z0-9_-]+',a.attempt)
 adoption=s.load(H/'root-adoption.json');source=adoption['source_commit'];cache=s.CACHES[a.lane];out=H/'runs'/(a.lane+'-'+a.attempt);out.mkdir(parents=True,exist_ok=False)
 env=s.environment(a.lane)
 for key in ('BIGLYBTC_PREVIEW_PROFILE','BIGLYBTC_HTTP_PRIVATE_RUNTIME','ASAN_OPTIONS','LSAN_OPTIONS','UBSAN_OPTIONS','QT_TLS_BACKEND','LD_DEBUG_OUTPUT'):env.pop(key,None)
 if a.lane=='san':env.update(ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
 for key in ('HOME','XDG_CONFIG_HOME','XDG_CACHE_HOME','XDG_RUNTIME_DIR'):
  path=out/key.lower();path.mkdir(mode=0o700);env[key]=str(path)
 runner=s.Runner(out,env);suites=suites_for_lane(a.lane);products=PRODUCTS if a.lane=='gcc' else {};targets=[*suites.values(),*products]
 result={'status':'FAIL','source_commit':source,'lane':a.lane,'cache':str(cache),'compiled_targets':targets,'provider_rebuilt':False,'historical_passes_pooled':False,'runner_sha256':s.sha(__file__),'source_receipt_sha256':s.sha(H/'root-adoption.json')}
 try:
  before=s.source_snapshot(source);s.write(out/'source-before.json',before);s.write(out/'native-source-map.json',{k:v for k,v in before['files_sha256'].items() if k.startswith('preview/native/')})
  for name,row in adoption['source_paths'].items():assert s.sha(N/name)==row['after_sha256'],name
  binding=N/'preview/native/engine/runtime-binding/provider-mainline-routing-runtime.json';assert s.sha(binding)=='f746fd279755ea90f31e20b0c9131c46e5f0871c4aab3665f232975f98feb56e'
  provider=s.load(binding);pin='355f0c7bca7404c3199e85d4741a3734ac460c79d641a2539c049c5a8ad0c443';assert s.sha(provider['artifact'])==pin;result['provider_sha256']=pin
  result['dist_manifest_before']=s.sha(N/'dist/BUILD_MANIFEST.json')
  runner.run('build',['nice','-n','5','cmake','--build',cache,'--parallel','1','--target',*targets],timeout=2400)
  receipt=dict(runner.rows[-1],source_manifest_sha256=s.sha(out/'native-source-map.json'));s.write(out/'build-receipt.json',receipt)
  paths={**{name:cache/'tests'/name for name in suites.values()},**{name:cache/path for name,path in products.items()}};images=s.images(paths);s.write(out/'images-before.json',images)
  try:runner.run('tests',['nice','-n','5','ctest','--test-dir',cache,'--parallel','1','-R','^('+'|'.join(suites)+')$','--output-on-failure','--output-junit',out/'tests.xml'],timeout=1000)
  finally:shutil.copyfile(cache/'Testing/Temporary/LastTest.log',out/'ctest-full.log')
  actual=s.passed_xml(out/'tests.xml',suites);assert all(c.get('status')=='run' for c in ET.parse(out/'tests.xml').getroot().iter('testcase'))
  assert not re.search(r'^(?:SKIP\s*:|FAIL!)',(out/'ctest-full.log').read_text(errors='replace'),re.M)
  assert s.source_snapshot(source)==before and s.images(paths)==images
  assert s.sha(provider['artifact'])==pin and s.sha(N/'dist/BUILD_MANIFEST.json')==result['dist_manifest_before']
  result.update(status='PASS_PROFILE_IO_I2CP_TRACKER_COHORT',suite_names=actual,actual_suites=len(actual),full_ctest_log_sha256=s.sha(out/'ctest-full.log'),tests_xml_sha256=s.sha(out/'tests.xml'),source_unchanged=True,images_unchanged=True,sanitizer_environment={k:env[k] for k in ('ASAN_OPTIONS','UBSAN_OPTIONS') if k in env})
 except BaseException as exc:result['failure']=repr(exc)
 finally:
  result['commands_sha256']=s.sha(out/'commands.json') if (out/'commands.json').exists() else None;s.write(out/'result.json',result)
 print(result['status'],out/'result.json',flush=True);return 0 if result['status'].startswith('PASS_') else 1
if __name__=='__main__':raise SystemExit(main())
