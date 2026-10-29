#!/usr/bin/env python3
"""One combined saved-SOCKS5 and I2P codec batch; no provider rebuild."""
import argparse, importlib.util, json, pathlib, re, shutil, subprocess, xml.etree.ElementTree as ET
R=pathlib.Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';H=R/'build/proxy-i2p-followup-20261029'
spec=importlib.util.spec_from_file_location('support',R/'build/mainline-scrape-20261028/recipe_support_followup.py');support=importlib.util.module_from_spec(spec);spec.loader.exec_module(support)
a=argparse.ArgumentParser();a.add_argument('lane',choices=['gcc','clang','san']);a.add_argument('--attempt',required=True);args=a.parse_args()
source='ecd63457da93e7f0c993c1f684e6d50d1c79a48c';out=H/'runs'/(args.lane+'-'+args.attempt);out.mkdir(parents=True,exist_ok=False)
prior=R/'build/network-ui-followup-20261029/runs'/(args.lane+'-r2/result.json');old=support.load(prior);cache=pathlib.Path(old['cache']);assert old['status']=='PASS_FOCUSED_NETWORK_UI_REPAIR_IDENTICAL_OTHER_IMAGES'
old_images=support.load(prior.parent/'images-before.json')
env=support.environment(args.lane)
for key in ('BIGLYBTC_PREVIEW_PROFILE','BIGLYBTC_HTTP_PRIVATE_RUNTIME','ASAN_OPTIONS','LSAN_OPTIONS','UBSAN_OPTIONS','QT_TLS_BACKEND','LD_DEBUG_OUTPUT'):env.pop(key,None)
if args.lane=='san':env.update(ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
for key in ('HOME','XDG_CONFIG_HOME','XDG_CACHE_HOME','XDG_RUNTIME_DIR'):
 p=out/key.lower();p.mkdir(mode=0o700);env[key]=str(p)
runner=support.Runner(out,env)
targets=['biglybtc-mainline-single-http-tests','biglybtc-managed-queue-tests','biglybtc-network-policy-tests','biglybtc-queue-policy-tests','biglybtc-mainline-single-tracker-owner-tests','biglybtc-i2p-datagram-tests','BiglyBTC','BiglyBTC-headless','biglybtc-preview-smoke']
suites=['network-policy','queue-policy','pure-i2p-datagram-codec']
if args.lane!='clang':suites+=['managed-queue','mainline-single-tracker-owner','mainline-single-http']

result={'status':'FAIL','source_commit':source,'lane':args.lane,'prior_qualification_sha256':support.sha(prior),'historical_passes_pooled':False,'provider_rebuilt':False,'compiled_targets':targets,'compile_only_suites':(['mainline-single-http','managed-queue','mainline-single-tracker-owner'] if args.lane=='clang' else []),'cache':str(cache),'runner_sha256':support.sha(__file__),'source_receipt_sha256':support.sha(H/'root-adoption.json')}
try:
 before=support.source_snapshot(source);support.write(out/'source-before.json',before)
 adoption=support.load(H/'root-adoption.json');assert adoption['source_commit']==source and adoption['source_path_count']==52
 for name,value in adoption['source_paths'].items():assert support.sha(N/name)==value['after_sha256'],name
 binding=N/'preview/native/engine/runtime-binding/provider-mainline-routing-runtime.json';assert support.sha(binding)=='f746fd279755ea90f31e20b0c9131c46e5f0871c4aab3665f232975f98feb56e';provider=support.load(binding);assert support.sha(provider['artifact'])=='355f0c7bca7404c3199e85d4741a3734ac460c79d641a2539c049c5a8ad0c443';result['provider_sha256']=support.sha(provider['artifact']);result['dist_manifest_before']=support.sha(N/'dist/BUILD_MANIFEST.json')
 runner.run('build',['nice','-n','5','cmake','--build',cache,'--parallel','1','--target',*targets],timeout=2400)
 paths={name:pathlib.Path(old_images[name]['path']) if name in old_images else cache/'tests'/name for name in targets}
 assert all(p.is_file() for p in paths.values()),'Missing built product'
 images=support.images(paths);support.write(out/'images-before.json',images)
 linked=runner.run('http-loader',['ldd',paths['biglybtc-mainline-single-http-tests']],timeout=30).read_text();match=re.search(r'libtorrent-rasterbar[^ ]* => (/.*?) \(',linked);assert match and support.sha(match.group(1))==result['provider_sha256']
 try:runner.run('tests',['nice','-n','5','ctest','--test-dir',cache,'--parallel','1','-R','^('+'|'.join(suites)+')$','--output-on-failure','--output-junit',out/'tests.xml'],timeout=900)
 finally:shutil.copyfile(cache/'Testing/Temporary/LastTest.log',out/'ctest-full.log')
 actual=support.passed_xml(out/'tests.xml',suites)
 full=(out/'ctest-full.log').read_text(errors='replace');assert not re.search(r'^(?:SKIP\s*:|FAIL!)',full,re.M),'Skipped/failed Qt method'
 methods=['regularTrackerTooltipKeepsActualRetainedCountsSeparateFromMainlineEstimate']
 runner.run('wayland-smoke',[paths['biglybtc-preview-smoke'],*methods,'-o',str(out/'wayland-smoke.xml')+',junitxml'],timeout=60)
 smoke=support.passed_xml(out/'wayland-smoke.xml');assert set(methods).issubset({name.split('::')[-1].split('(')[0] for name in smoke}),smoke
 result.update(smoke_methods=methods,full_ctest_log_sha256=support.sha(out/'ctest-full.log'))
 assert support.source_snapshot(source)==before,'Native source changed';assert support.images(paths)==images,'Built image changed';assert support.sha(provider['artifact'])==result['provider_sha256'],'Provider changed';assert support.sha(N/'dist/BUILD_MANIFEST.json')==result['dist_manifest_before'],'Dist changed'
 result.update(status='PASS_PROXY_I2P_FOCUSED_COHORT',suite_names=actual,actual_suites=len(actual),tests_xml_sha256=support.sha(out/'tests.xml'),source_unchanged=True,images_unchanged=True,sanitizer_environment={k:env[k] for k in ('ASAN_OPTIONS','UBSAN_OPTIONS') if k in env})
except BaseException as exc:result['failure']=repr(exc)
finally:
 result['commands_sha256']=support.sha(out/'commands.json') if (out/'commands.json').exists() else None;support.write(out/'result.json',result)
print(result['status'],out/'result.json',flush=True)
raise SystemExit(0 if result['status'].startswith('PASS_') else 1)
