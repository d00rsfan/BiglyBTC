#!/usr/bin/env python3
"""Reconfigure the user-requested HTTP budget; rerun only HTTP with identical ELFs."""
import argparse,importlib.util,json,pathlib,re,shutil,xml.etree.ElementTree as ET
R=pathlib.Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';H=pathlib.Path(__file__).parent
spec=importlib.util.spec_from_file_location('s',R/'build/mainline-scrape-20261028/recipe_support_followup.py');s=importlib.util.module_from_spec(spec);spec.loader.exec_module(s)
a=argparse.ArgumentParser();a.add_argument('lane',choices=['gcc','clang','san']);args=a.parse_args()
receipt=s.load(H/'timeout-adoption.json');source=receipt['source_commit'];before=s.source_snapshot(source);prior=H/'runs'/(args.lane+'-r1');old=s.load(prior/'result.json');cache=pathlib.Path(old['cache']);images=s.load(prior/'images-before.json');paths={k:pathlib.Path(v['path']) for k,v in images.items()}
out=H/'runs'/(args.lane+'-r3');out.mkdir(parents=True,exist_ok=False);env=s.environment(args.lane)
for key in ('BIGLYBTC_PREVIEW_PROFILE','BIGLYBTC_HTTP_PRIVATE_RUNTIME','ASAN_OPTIONS','LSAN_OPTIONS','UBSAN_OPTIONS','QT_TLS_BACKEND','LD_DEBUG_OUTPUT'):env.pop(key,None)
if args.lane=='san':env.update(ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
for key in ('HOME','XDG_CONFIG_HOME','XDG_CACHE_HOME','XDG_RUNTIME_DIR'):
 p=out/key.lower();p.mkdir(mode=0o700);env[key]=str(p)
runner=s.Runner(out,env);result={'status':'FAIL','source_commit':source,'code_build_source_commit':receipt['implementation_commit'],'lane':args.lane,'cache':str(cache),'cpp_builds_executed':False,'provider_rebuilt':False,'runner_sha256':s.sha(__file__),'source_receipt_sha256':s.sha(H/'root-adoption-r2.json'),'compile_only_suites':old['compile_only_suites'],'historical_passes_pooled':False}
try:
 assert old['status']=='FAIL' and old['source_commit']==receipt['implementation_commit']
 oldsource=s.load(prior/'source-before.json');changed={k for k in before['files_sha256'] if oldsource['files_sha256'].get(k)!=before['files_sha256'][k]}
 assert set(oldsource['files_sha256'])==set(before['files_sha256']) and changed=={receipt['only_changed_path']}
 assert oldsource['files_sha256'][receipt['only_changed_path']]==receipt['before_sha256'] and before['files_sha256'][receipt['only_changed_path']]==receipt['after_sha256']
 wanted={'queue-policy','managed-queue','mainline-single-http','pure-i2p-datagram-codec','pure-i2p-client-identity'} | (set() if args.lane=='clang' else {'mainline-single-tracker-owner'})
 rows=list(ET.parse(prior/'tests.xml').getroot().iter('testcase'));assert len(rows)==len(wanted) and {r.get('name') for r in rows}==wanted
 assert {r.get('name') for r in rows if r.find('failure') is not None}=={'mainline-single-http'}
 assert not any(r.find(tag) is not None for r in rows for tag in ('error','skipped'))
 reused=sorted(wanted-{'mainline-single-http'});assert all(r.get('status')=='run' for r in rows if r.get('name') in reused)
 assert not re.search(r'^(?:SKIP\s*:|FAIL!)',(prior/'ctest-full.log').read_text(),re.M),'Unexpected inner failure in timed-out prior run'
 assert s.images(paths)==images;dist_pin=s.sha(N/'dist/BUILD_MANIFEST.json');provider=s.load(N/'preview/native/engine/runtime-binding/provider-mainline-routing-runtime.json');assert s.sha(provider['artifact'])=='355f0c7bca7404c3199e85d4741a3734ac460c79d641a2539c049c5a8ad0c443'
 s.write(out/'source-before.json',before);s.write(out/'images-before.json',images)
 audit={'prior_source_commit':old['source_commit'],'current_source_commit':source,'only_changed_source':receipt['only_changed_path'],'reused_passed_suites':reused,'all_built_images_identical':True,'prior_evidence_sha256':{name:s.sha(prior/name) for name in ('result.json','commands.json','images-before.json','source-before.json','tests.xml','ctest-full.log')}};s.write(out/'reuse-audit.json',audit)
 runner.run('configure',['nice','-n','5','cmake','-S',N/'preview/native','-B',cache],timeout=180)
 assert s.images(paths)==images,'Configuration changed an ELF'
 definition=(cache/'tests/CTestTestfile.cmake').read_text();budget_lines=[line for line in definition.splitlines() if line.startswith("set_tests_properties([=[mainline-single-http]=] ")]
 assert len(budget_lines)==1 and 'TIMEOUT "1000"' in budget_lines[0],'HTTP CTest budget not applied'
 try:runner.run('tests',['nice','-n','5','ctest','--test-dir',cache,'--parallel','1','-R','^mainline-single-http$','--output-on-failure','--output-junit',out/'tests.xml'],timeout=1100)
 finally:shutil.copyfile(cache/'Testing/Temporary/LastTest.log',out/'ctest-full.log')
 actual=s.passed_xml(out/'tests.xml',['mainline-single-http']);assert not re.search(r'^(?:SKIP\s*:|FAIL!)',(out/'ctest-full.log').read_text(),re.M)
 assert s.source_snapshot(source)==before and s.images(paths)==images and s.sha(N/'dist/BUILD_MANIFEST.json')==dist_pin
 assert s.sha(provider['artifact'])=='355f0c7bca7404c3199e85d4741a3734ac460c79d641a2539c049c5a8ad0c443'
 result.update(status='PASS_HTTP_TIMEOUT_REPAIR_IDENTICAL_IMAGES',actual_suites=len(actual),suite_names=actual,qualified_suites=sorted(wanted),reused_passed_suites=reused,reuse_audit_sha256=s.sha(out/'reuse-audit.json'),source_unchanged=True,images_unchanged=True,tests_xml_sha256=s.sha(out/'tests.xml'),full_ctest_log_sha256=s.sha(out/'ctest-full.log'),provider_sha256=s.sha(provider['artifact']),sanitizer_environment={k:env[k] for k in ('ASAN_OPTIONS','UBSAN_OPTIONS') if k in env})
except BaseException as exc:result['failure']=repr(exc)
finally:
 result['commands_sha256']=s.sha(out/'commands.json') if (out/'commands.json').exists() else None;s.write(out/'result.json',result)
print(result['status'],out/'result.json',flush=True)
raise SystemExit(0 if result['status'].startswith('PASS_') else 1)
