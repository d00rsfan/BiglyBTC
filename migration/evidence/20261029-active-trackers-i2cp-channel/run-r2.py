#!/usr/bin/env python3
"""One combined active-tracker/initial-I2CP channel qualification batch."""
import argparse, importlib.util, pathlib, re, shutil
import xml.etree.ElementTree as ET

R=pathlib.Path('/home/yurii/Java/BiglyBT'); N=R.parent/'BiglyBTC'; H=pathlib.Path(__file__).parent
spec=importlib.util.spec_from_file_location('support',R/'build/mainline-scrape-20261028/recipe_support_followup.py')
s=importlib.util.module_from_spec(spec);spec.loader.exec_module(s)
p=argparse.ArgumentParser();p.add_argument('lane',choices=['gcc','clang','san']);p.add_argument('--attempt',required=True)
a=p.parse_args();assert re.fullmatch('[A-Za-z0-9_-]+',a.attempt)
adoption=s.load(H/'root-adoption-r2.json');source=adoption['source_commit']
prior=R/'build/mixed-i2cp-followup-20261029/runs'/(a.lane+'-r1/result.json')
old=s.load(prior);assert old['status']=='PASS_MIXED_I2CP_COHORT';cache=pathlib.Path(old['cache'])
out=H/'runs'/(a.lane+'-'+a.attempt);out.mkdir(parents=True,exist_ok=False)
env=s.environment(a.lane)
for key in ('BIGLYBTC_PREVIEW_PROFILE','BIGLYBTC_HTTP_PRIVATE_RUNTIME','ASAN_OPTIONS','LSAN_OPTIONS','UBSAN_OPTIONS','QT_TLS_BACKEND','LD_DEBUG_OUTPUT'):
 env.pop(key,None)
if a.lane=='san':env.update(ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
for key in ('HOME','XDG_CONFIG_HOME','XDG_CACHE_HOME','XDG_RUNTIME_DIR'):
 path=out/key.lower();path.mkdir(mode=0o700);env[key]=str(path)
runner=s.Runner(out,env)
targets=['biglybtc-managed-queue-tests','biglybtc-queue-policy-tests','biglybtc-mainline-single-tracker-owner-tests',
 'biglybtc-mainline-single-http-tests','biglybtc-i2cp-initial-channel-tests','BiglyBTC','BiglyBTC-headless','biglybtc-preview-smoke']
suites=['queue-policy','managed-queue','i2cp-initial-channel']
selector='directAdmittedTrackerUsesActualAnnounceOrSavedTrustWithoutMainline:private-HTTPS-saved-JKS-regular-scrape'
result={'status':'FAIL','source_commit':source,'lane':a.lane,'cache':str(cache),'compiled_targets':targets,
 'compile_only_suites':['mainline-single-tracker-owner'],'historical_passes_pooled':False,'provider_rebuilt':False,
 'prior_qualification_sha256':s.sha(prior),'runner_sha256':s.sha(__file__),'source_receipt_sha256':s.sha(H/'root-adoption-r2.json')}
try:
 before=s.source_snapshot(source);s.write(out/'source-before.json',before)
 assert len(adoption['source_paths'])==adoption['source_path_count']
 for name,row in adoption['source_paths'].items():assert s.sha(N/name)==row['after_sha256'],name
 binding=N/'preview/native/engine/runtime-binding/provider-mainline-routing-runtime.json'
 assert s.sha(binding)=='f746fd279755ea90f31e20b0c9131c46e5f0871c4aab3665f232975f98feb56e'
 provider=s.load(binding);pin='355f0c7bca7404c3199e85d4741a3734ac460c79d641a2539c049c5a8ad0c443'
 assert s.sha(provider['artifact'])==pin;result['provider_sha256']=pin
 result['dist_manifest_before']=s.sha(N/'dist/BUILD_MANIFEST.json')
 runner.run('build',['nice','-n','5','cmake','--build',cache,'--parallel','1','--target',*targets],timeout=2400)
 old_images=s.load(prior.parent/'images-before.json')
 paths={name:pathlib.Path(old_images[name]['path']) if name in old_images else cache/'tests'/name for name in targets}
 assert all(path.is_file() for path in paths.values());images=s.images(paths);s.write(out/'images-before.json',images)
 linked=runner.run('http-loader',['ldd',paths['biglybtc-mainline-single-http-tests']],timeout=30).read_text()
 match=re.search(r'libtorrent-rasterbar[^ ]* => (/.*?) \(',linked);assert match and s.sha(match.group(1))==pin
 try:
  runner.run('tests',['nice','-n','5','ctest','--test-dir',cache,'--parallel','1','-R','^('+'|'.join(suites)+')$',
   '--output-on-failure','--output-junit',out/'tests.xml'],timeout=600)
 finally:shutil.copyfile(cache/'Testing/Temporary/LastTest.log',out/'ctest-full.log')
 actual=s.passed_xml(out/'tests.xml',suites)
 full=(out/'ctest-full.log').read_text(errors='replace');assert not re.search(r'^(?:SKIP\s*:|FAIL!)',full,re.M)
 runner.run('sole-https-regression',['nice','-n','5',paths['biglybtc-mainline-single-http-tests'],selector,
  '-o',str(out/'sole-https-regression.txt')+',txt','-o',str(out/'sole-https-regression.xml')+',junitxml'],timeout=180)
 selected=s.passed_xml(out/'sole-https-regression.xml');assert len(selected)==3
 assert {row.rsplit('::',1)[-1].removesuffix('()') for row in selected}=={
  'initTestCase','cleanupTestCase','directAdmittedTrackerUsesActualAnnounceOrSavedTrustWithoutMainline(private-HTTPS-saved-JKS-regular-scrape)'}
 assert all(row.get('status')=='run' for file in ('tests.xml','sole-https-regression.xml') for row in ET.parse(out/file).getroot().iter('testcase'))
 assert not re.search(r'^(?:SKIP\s*:|FAIL!)',(out/'sole-https-regression.txt').read_text(),re.M)
 assert s.source_snapshot(source)==before and s.images(paths)==images
 assert s.sha(provider['artifact'])==pin and s.sha(N/'dist/BUILD_MANIFEST.json')==result['dist_manifest_before']
 result.update(status='PASS_ACTIVE_CHANNEL_COHORT',suite_names=actual,actual_suites=len(actual),
  full_ctest_log_sha256=s.sha(out/'ctest-full.log'),tests_xml_sha256=s.sha(out/'tests.xml'),
  sole_https_selector=selector,sole_https_xml_sha256=s.sha(out/'sole-https-regression.xml'),
  source_unchanged=True,images_unchanged=True,sanitizer_environment={k:env[k] for k in ('ASAN_OPTIONS','UBSAN_OPTIONS') if k in env})
except BaseException as exc:result['failure']=repr(exc)
finally:
 result['commands_sha256']=s.sha(out/'commands.json') if (out/'commands.json').exists() else None
 s.write(out/'result.json',result)
print(result['status'],out/'result.json',flush=True)
raise SystemExit(0 if result['status'].startswith('PASS_') else 1)
