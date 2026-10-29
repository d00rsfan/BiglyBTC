#!/usr/bin/env python3
"""Focused build and actual HTTP/authority tests; existing caches, no provider rebuild."""
import argparse, importlib.util, json, pathlib, re, shutil, subprocess, xml.etree.ElementTree as ET
R=pathlib.Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';H=R/'build/network-ui-followup-20261029'
spec=importlib.util.spec_from_file_location('support',R/'build/mainline-scrape-20261028/recipe_support_followup.py');support=importlib.util.module_from_spec(spec);spec.loader.exec_module(support)
a=argparse.ArgumentParser();a.add_argument('lane',choices=['gcc','clang','san']);a.add_argument('--attempt',required=True);args=a.parse_args()
source='dfc27e723344203e49d96341a3039be444062858';out=H/'runs'/(args.lane+'-'+args.attempt);out.mkdir(parents=True,exist_ok=False)
prior=R/'build/mainline-host-cohort-20261028/amendments/provider-r2-recipes/qualification/runs'/(args.lane+'-r4/result.json');old=support.load(prior);cache=pathlib.Path(old['cache']);assert old['status']=='PASS_ACTUAL_SELECTED_COHORT'
env=support.environment(args.lane)
for key in ('BIGLYBTC_PREVIEW_PROFILE','BIGLYBTC_HTTP_PRIVATE_RUNTIME','ASAN_OPTIONS','LSAN_OPTIONS','UBSAN_OPTIONS','QT_TLS_BACKEND','LD_DEBUG_OUTPUT'):env.pop(key,None)
if args.lane=='san':env.update(ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
for key in ('HOME','XDG_CONFIG_HOME','XDG_CACHE_HOME','XDG_RUNTIME_DIR'):
 p=out/key.lower();p.mkdir(mode=0o700);env[key]=str(p)
runner=support.Runner(out,env)
targets=['biglybtc-mainline-single-http-tests','biglybtc-mainline-single-udp-tests','biglybtc-mainline-single-tracker-owner-tests','biglybtc-mainline-dht-tests','biglybtc-mainline-original-host-tests','biglybtc-tor-owned-child-tests','biglybtc-queue-policy-tests','biglybtc-network-policy-tests','BiglyBTC','BiglyBTC-headless','biglybtc-preview-smoke']
suites=['queue-policy','network-policy','mainline-dht']
if args.lane!='clang':
 suites+=['mainline-single-http','mainline-single-udp','mainline-single-tracker-owner','owned-tor-child-lifecycle']
 artifact=R/'build/dependencies/tor-owned-0.4.9.11/runtime-fixture-artifact.json';assert support.sha(artifact)=='25f0d75d2a2cc482b3e912457099ee666622444a0ae714151257844ca5987356';tor=support.load(artifact)
 for key in ('launcher','tor_artifact'):assert support.sha(tor[key]['path'])==tor[key]['sha256']
 env.update(BIGLYBTC_OWNED_TOR_EXECUTABLE=tor['launcher']['path'],BIGLYBTC_OWNED_TOR_RUNTIME_ARTIFACT=str(artifact),BIGLYBTC_OWNED_TOR_RUNTIME_ARTIFACT_SHA256=support.sha(artifact),BIGLYBTC_OWNED_TOR_RESULT=str(out/'real-tor-observed.json'))
result={'status':'FAIL','source_commit':source,'lane':args.lane,'prior_qualification_sha256':support.sha(prior),'historical_passes_pooled':False,'provider_rebuilt':False,'compiled_targets':targets,'compile_only_suites':['mainline-original-host-identity']+(['mainline-single-http','mainline-single-udp','mainline-single-tracker-owner','owned-tor-child-lifecycle'] if args.lane=='clang' else []),'cache':str(cache),'runner_sha256':support.sha(__file__),'source_receipt_sha256':support.sha(H/'root-adoption.json')}
try:
 before=support.source_snapshot(source);support.write(out/'source-before.json',before)
 adoption=support.load(H/'root-adoption.json');assert adoption['source_commit']==source and adoption['source_path_count']==20
 for name,value in adoption['source_paths'].items():assert support.sha(N/name)==value['after_sha256'],name
 binding=N/'preview/native/engine/runtime-binding/provider-mainline-routing-runtime.json';assert support.sha(binding)=='f746fd279755ea90f31e20b0c9131c46e5f0871c4aab3665f232975f98feb56e';provider=support.load(binding);assert support.sha(provider['artifact'])=='355f0c7bca7404c3199e85d4741a3734ac460c79d641a2539c049c5a8ad0c443';result['provider_sha256']=support.sha(provider['artifact']);result['dist_manifest_before']=support.sha(N/'dist/BUILD_MANIFEST.json')
 runner.run('build',['nice','-n','5','cmake','--build',cache,'--parallel','1','--target',*targets],timeout=1800)
 paths={name:pathlib.Path(old['images_after'][name]['path']) if name in old['images_after'] else cache/'tests'/name for name in targets}
 assert all(p.is_file() for p in paths.values()),'Missing built product'
 images=support.images(paths);support.write(out/'images-before.json',images)
 linked=runner.run('http-loader',['ldd',paths['biglybtc-mainline-single-http-tests']],timeout=30).read_text();match=re.search(r'libtorrent-rasterbar[^ ]* => (/.*?) \(',linked);assert match and support.sha(match.group(1))==result['provider_sha256']
 runner.run('tests',['nice','-n','5','ctest','--test-dir',cache,'--parallel','1','-R','^('+'|'.join(suites)+')$','--output-on-failure','--output-junit',out/'tests.xml'],timeout=600)
 actual=support.passed_xml(out/'tests.xml',suites)
 shutil.copyfile(cache/'Testing/Temporary/LastTest.log',out/'ctest-full.log');full=(out/'ctest-full.log').read_text(errors='replace');assert not re.search(r'^(?:SKIP\s+:|FAIL!)',full,re.M),'Skipped/failed Qt method'
 methods=['exactWaylandNoFocusWarningKeepsOtherDiagnosticsAndForwardHandler','explicitQtConsoleControlsDisableNativeFiltering']
 runner.run('wayland-smoke',[paths['biglybtc-preview-smoke'],*methods,'-o',str(out/'wayland-smoke.xml')+',junitxml'],timeout=60)
 smoke=support.passed_xml(out/'wayland-smoke.xml');assert set(methods).issubset({name.split('::')[-1].split('(')[0] for name in smoke}),smoke
 pylog=runner.run('launcher-units',['python3','-B',N/'preview/native/packaging/test_preview.py'],timeout=60).read_text();count=re.search(r'Ran (\d+) tests?',pylog);assert count and int(count.group(1))>0
 result.update(wayland_smoke_methods=methods,launcher_unit_tests=int(count.group(1)),full_ctest_log_sha256=support.sha(out/'ctest-full.log'))
 assert support.source_snapshot(source)==before,'Native source changed';assert support.images(paths)==images,'Built image changed';assert support.sha(provider['artifact'])==result['provider_sha256'],'Provider changed';assert support.sha(N/'dist/BUILD_MANIFEST.json')==result['dist_manifest_before'],'Dist changed'
 result.update(status='PASS_FOCUSED_NETWORK_UI_FOLLOWUP',suite_names=actual,actual_suites=len(actual),tests_xml_sha256=support.sha(out/'tests.xml'),source_unchanged=True,images_unchanged=True,sanitizer_environment={k:env[k] for k in ('ASAN_OPTIONS','UBSAN_OPTIONS') if k in env})
except BaseException as exc:result['failure']=repr(exc)
finally:
 result['commands_sha256']=support.sha(out/'commands.json') if (out/'commands.json').exists() else None;support.write(out/'result.json',result)
print(result['status'],out/'result.json',flush=True)
raise SystemExit(0 if result['status'].startswith('PASS_') else 1)
