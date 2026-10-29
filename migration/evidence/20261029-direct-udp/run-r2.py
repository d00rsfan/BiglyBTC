#!/usr/bin/env python3
"""One DIRECT UDP regular-tracker batch; existing protocol/provider and I2P unchanged."""
import argparse, importlib.util, json, pathlib, re, shutil, subprocess, xml.etree.ElementTree as ET
R=pathlib.Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';H=R/'build/direct-udp-followup-20261029'
spec=importlib.util.spec_from_file_location('support',R/'build/mainline-scrape-20261028/recipe_support_followup.py');support=importlib.util.module_from_spec(spec);spec.loader.exec_module(support)
a=argparse.ArgumentParser();a.add_argument('lane',choices=['gcc','clang','san']);a.add_argument('--attempt',required=True);args=a.parse_args()
source='0cb5817bf07c6268bcccf9722f8c741a904163c6';out=H/'runs'/(args.lane+'-'+args.attempt);out.mkdir(parents=True,exist_ok=False)
prior=R/'build/direct-i2p-identity-followup-20261029/runs'/(args.lane+'-r3/result.json');old=support.load(prior);cache=pathlib.Path(old['cache']);assert old['status']=='PASS_HTTP_TIMEOUT_REPAIR_IDENTICAL_IMAGES'
old_images=support.load(prior.parent/'images-before.json')
env=support.environment(args.lane)
for key in ('BIGLYBTC_PREVIEW_PROFILE','BIGLYBTC_HTTP_PRIVATE_RUNTIME','ASAN_OPTIONS','LSAN_OPTIONS','UBSAN_OPTIONS','QT_TLS_BACKEND','LD_DEBUG_OUTPUT'):env.pop(key,None)
if args.lane=='san':env.update(ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
for key in ('HOME','XDG_CONFIG_HOME','XDG_CACHE_HOME','XDG_RUNTIME_DIR'):
 p=out/key.lower();p.mkdir(mode=0o700);env[key]=str(p)
runner=support.Runner(out,env)
targets=['biglybtc-mainline-single-http-tests','biglybtc-managed-queue-tests','biglybtc-queue-policy-tests','biglybtc-mainline-single-tracker-owner-tests','BiglyBTC','BiglyBTC-headless','biglybtc-preview-smoke']
suites=['queue-policy','managed-queue','mainline-single-http']
if args.lane!='clang':suites+=['mainline-single-tracker-owner']

result={'status':'FAIL','source_commit':source,'lane':args.lane,'prior_qualification_sha256':support.sha(prior),'historical_passes_pooled':False,'provider_rebuilt':False,'compiled_targets':targets,'compile_only_suites':(['mainline-single-tracker-owner'] if args.lane=='clang' else []),'cache':str(cache),'runner_sha256':support.sha(__file__),'source_receipt_sha256':support.sha(H/'root-adoption-r2.json')}
try:
 before=support.source_snapshot(source);support.write(out/'source-before.json',before)
 adoption=support.load(H/'root-adoption-r2.json');assert adoption['source_commit']==source and adoption['source_path_count']==11
 for name,value in adoption['source_paths'].items():assert support.sha(N/name)==value['after_sha256'],name
 binding=N/'preview/native/engine/runtime-binding/provider-mainline-routing-runtime.json';assert support.sha(binding)=='f746fd279755ea90f31e20b0c9131c46e5f0871c4aab3665f232975f98feb56e';provider=support.load(binding);assert support.sha(provider['artifact'])=='355f0c7bca7404c3199e85d4741a3734ac460c79d641a2539c049c5a8ad0c443';result['provider_sha256']=support.sha(provider['artifact']);result['dist_manifest_before']=support.sha(N/'dist/BUILD_MANIFEST.json')
 runner.run('build',['nice','-n','5','cmake','--build',cache,'--parallel','1','--target',*targets],timeout=2400)
 paths={name:pathlib.Path(old_images[name]['path']) if name in old_images else cache/'tests'/name for name in targets}
 assert all(p.is_file() for p in paths.values()),'Missing built product'
 images=support.images(paths);support.write(out/'images-before.json',images)
 linked=runner.run('http-loader',['ldd',paths['biglybtc-mainline-single-http-tests']],timeout=30).read_text();match=re.search(r'libtorrent-rasterbar[^ ]* => (/.*?) \(',linked);assert match and support.sha(match.group(1))==result['provider_sha256']
 try:runner.run('tests',['nice','-n','5','ctest','--test-dir',cache,'--parallel','1','-R','^('+'|'.join(suites)+')$','--output-on-failure','--output-junit',out/'tests.xml'],timeout=1800)
 finally:shutil.copyfile(cache/'Testing/Temporary/LastTest.log',out/'ctest-full.log')
 actual=support.passed_xml(out/'tests.xml',suites)
 full=(out/'ctest-full.log').read_text(errors='replace');assert not re.search(r'^(?:SKIP\s*:|FAIL!)',full,re.M),'Skipped/failed Qt method'
 if args.lane=='clang':
  methods=['directRegularUdpSuccessfulScrapeUsesTheOriginalSharedInterval','directRegularUdpSavedProxyChangesCannotKeepDirectAuthority','directRegularUdpSavedRowsRequireTrackerButNotDhtOrPayload','directRegularUdpActualAdmissionSharesResponseAndCannotDeliverAfterPause','directRegularUdpFreshTrackerDenialCancelsConnectOrReply']
  runner.run('udp-selected',[paths['biglybtc-mainline-single-tracker-owner-tests'],*methods,'-o',str(out/'udp-selected.xml')+',junitxml'],timeout=240)
  rows=support.passed_xml(out/'udp-selected.xml');assert len(rows)==12 and {name.split('::')[-1].split('(')[0] for name in rows}==set(methods)|{'initTestCase','cleanupTestCase'},rows
  result.update(actual_selected_udp_rows=10,selected_udp_xml_sha256=support.sha(out/'udp-selected.xml'))
 result.update(full_ctest_log_sha256=support.sha(out/'ctest-full.log'))
 assert support.source_snapshot(source)==before,'Native source changed';assert support.images(paths)==images,'Built image changed';assert support.sha(provider['artifact'])==result['provider_sha256'],'Provider changed';assert support.sha(N/'dist/BUILD_MANIFEST.json')==result['dist_manifest_before'],'Dist changed'
 result.update(status='PASS_DIRECT_UDP_COHORT',suite_names=actual,actual_suites=len(actual),tests_xml_sha256=support.sha(out/'tests.xml'),source_unchanged=True,images_unchanged=True,sanitizer_environment={k:env[k] for k in ('ASAN_OPTIONS','UBSAN_OPTIONS') if k in env})
except BaseException as exc:result['failure']=repr(exc)
finally:
 result['commands_sha256']=support.sha(out/'commands.json') if (out/'commands.json').exists() else None;support.write(out/'result.json',result)
print(result['status'],out/'result.json',flush=True)
raise SystemExit(0 if result['status'].startswith('PASS_') else 1)
