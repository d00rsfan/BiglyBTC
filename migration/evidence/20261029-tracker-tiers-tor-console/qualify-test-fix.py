#!/usr/bin/env python3
"""Recheck queue/Tor repairs and missing checks; reuse only identical other ELFs."""
import argparse, importlib.util, json, pathlib, re, shlex, shutil, subprocess
from pathlib import Path
import xml.etree.ElementTree as ET
R=pathlib.Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';F=pathlib.Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('support',R/'build/mainline-scrape-20261028/recipe_support_followup.py');s=importlib.util.module_from_spec(spec);spec.loader.exec_module(s)
SUITE_IMAGES = {'queue-policy':'biglybtc-queue-policy-tests','network-policy':'biglybtc-network-policy-tests',
    'mainline-dht':'biglybtc-mainline-dht-tests','mainline-single-http':'biglybtc-mainline-single-http-tests',
    'mainline-single-udp':'biglybtc-mainline-single-udp-tests','mainline-single-tracker-owner':'biglybtc-mainline-single-tracker-owner-tests',
    'owned-tor-child-lifecycle':'biglybtc-tor-owned-child-tests'}
def actual_suite_commands(log, wanted, images):
    blocks={}; active=None
    for line in log.read_text(errors='replace').splitlines():
        match=re.fullmatch(r'\d+/\d+ Testing: (.+)',line)
        if match:
            active=match.group(1); assert active not in blocks, 'Repeated actual suite'; blocks[active]=[]
        elif active is not None: blocks[active].append(line)
    assert set(blocks)==set(wanted), 'Actual command inventory differs'
    for name,lines in blocks.items():
        commands=[shlex.split(line.removeprefix('Command: ')) for line in lines if line.startswith('Command: ')]
        assert len(commands)==1 and commands[0], 'Missing/ambiguous actual suite command'
        assert Path(commands[0][0]).resolve()==Path(images[SUITE_IMAGES[name]]['path']).resolve(), 'Suite ran a different ELF '+name
    return blocks

def validate_prior_origin(prior, lane, audit, digest, read):
    entry=audit['lanes'][lane]; pins=entry['evidence_sha256']
    assert {'tests.xml','ctest-full.log','result.json','commands.json','images-before.json','source-before.json'}<=set(pins)
    for name,pin in pins.items(): assert digest(prior/name)==pin, 'Prior evidence changed '+name
    wanted={'queue-policy','network-policy','mainline-dht'} | (set() if lane=='clang' else {'mainline-single-http','mainline-single-udp','mainline-single-tracker-owner','owned-tor-child-lifecycle'})
    failed={'queue-policy'} | (set() if lane=='clang' else {'owned-tor-child-lifecycle'})
    rows=list(ET.parse(prior/'tests.xml').getroot().iter('testcase'))
    assert len(rows)==len(wanted) and {row.get('name') for row in rows}==wanted, 'Prior exact inventory differs'
    assert not any(row.find(tag) is not None for row in rows for tag in ('error','skipped')), 'Prior error/skip'
    assert {row.get('name') for row in rows if row.find('failure') is not None}==failed
    passed=[]
    for row in rows:
        if row.get('name') in failed: assert row.get('status')=='fail'
        else: assert row.get('status')=='run' and row.find('failure') is None; passed.append(row.get('name'))
    assert sorted(passed)==sorted(entry['passed_suites']) and sorted(failed)==sorted(entry['failed_suites'])
    old=read(prior/'result.json'); oldsource=read(prior/'source-before.json'); images=read(prior/'images-before.json')
    assert old['status']=='FAIL' and old['source_commit']==oldsource['source_commit']==audit['source_commit']
    assert entry['all_images_unchanged'] and digest(prior/'source-before.json')==audit['source_snapshot_sha256']
    blocks=actual_suite_commands(prior/'ctest-full.log',wanted,images)
    for name in passed:
        assert not re.search(r'^(?:SKIP\s+:|FAIL!)','\n'.join(blocks[name]),re.M), 'Skipped/failed inner method '+name
    return passed,images

p=argparse.ArgumentParser();p.add_argument('lane',choices=['gcc','clang','san']);a=p.parse_args()
receipt=F/'final-source.json';adoption=s.load(receipt);source=adoption['source_commit'];changed={'preview/native/tests/queue_policy_tests.cpp','preview/native/services/tor_control/owned_child.cpp','preview/native/services/tor_control/owned_child_tests.cpp'}
audit=s.load(F/'r1-failure-audit.json');assert audit['status']=='ACTUAL_QUEUE_TOR_FAILURES_WITH_UNCHANGED_IMAGES'
prior=F/'runs'/(a.lane+'-r1');old=s.load(prior/'result.json');cache=pathlib.Path(old['cache']);out=F/'runs'/(a.lane+'-r2');out.mkdir(exist_ok=False)
env=s.environment(a.lane)
for key in ('BIGLYBTC_PREVIEW_PROFILE','BIGLYBTC_HTTP_PRIVATE_RUNTIME','LSAN_OPTIONS','QT_TLS_BACKEND','LD_DEBUG_OUTPUT'):env.pop(key,None)
for key in ('HOME','XDG_CONFIG_HOME','XDG_CACHE_HOME','XDG_RUNTIME_DIR'):
 d=out/key.lower();d.mkdir(mode=0o700);env[key]=str(d)
runner=s.Runner(out,env);result={'status':'FAIL','source_commit':source,'lane':a.lane,'cache':str(cache),'runner_sha256':s.sha(__file__),'source_receipt':str(receipt),'source_receipt_sha256':s.sha(receipt),'compiled_targets':['biglybtc-queue-policy-tests','biglybtc-tor-owned-child-tests'],'compile_only_suites':old['compile_only_suites'],'provider_sha256':old['provider_sha256'],'unchanged_image_evidence_reused':True,'provider_rebuilt':False,'prior_failure_audit_sha256':s.sha(F/'r1-failure-audit.json')}
try:
 before=s.source_snapshot(source);s.write(out/'source-before.json',before);oldsource=s.load(prior/'source-before.json')
 assert {k:v for k,v in before['files_sha256'].items() if k not in changed}=={k:v for k,v in oldsource['files_sha256'].items() if k not in changed},'Non-test source changed'
 assert subprocess.check_output(['git','-C',str(N),'diff','--name-only',old['source_commit'],source]).decode().splitlines()==sorted(changed)
 assert adoption['source_commit']==source and adoption['source_path_count']==len(adoption['source_paths'])==20
 for name,row in adoption['source_paths'].items():assert s.sha(N/name)==row['after_sha256'],name
 images=s.load(prior/'images-before.json');paths={k:pathlib.Path(v['path']) for k,v in images.items()}
 assert s.images(paths)==images,'Original built images changed before correction'
 reused,origin_images=validate_prior_origin(prior,a.lane,audit,s.sha,s.load)
 assert origin_images==images
 result['dist_manifest_before']=s.sha(N/'dist/BUILD_MANIFEST.json')
 binding=s.load(N/'preview/native/engine/runtime-binding/provider-mainline-routing-runtime.json');assert s.sha(binding['artifact'])==result['provider_sha256']
 runner.run('build',['nice','-n','5','cmake','--build',cache,'--parallel','1','--target','biglybtc-queue-policy-tests','biglybtc-tor-owned-child-tests'],timeout=600)
 current=s.images(paths)
 for name,row in images.items():
  if name not in {'biglybtc-queue-policy-tests','biglybtc-tor-owned-child-tests'}:assert current[name]==row,'Unrelated qualified ELF changed '+name
 s.write(out/'images-before.json',current)
 fresh=['queue-policy']+(['owned-tor-child-lifecycle'] if a.lane!='clang' else [])
 try:runner.run('tests',['nice','-n','5','ctest','--test-dir',cache,'--parallel','1','-R','^('+'|'.join(fresh)+')$','--output-on-failure','--output-junit',out/'tests.xml'],timeout=180)
 finally:shutil.copyfile(cache/'Testing/Temporary/LastTest.log',out/'ctest-full.log')
 actual=s.passed_xml(out/'tests.xml',fresh)
 fresh_blocks=actual_suite_commands(out/'ctest-full.log',fresh,current)
 assert not re.search(r'^(?:SKIP\s+:|FAIL!)',(out/'ctest-full.log').read_text(errors='replace'),re.M)
 methods=['exactWaylandNoFocusWarningKeepsOtherDiagnosticsAndForwardHandler','explicitQtConsoleControlsDisableNativeFiltering']
 runner.run('wayland-smoke',[paths['biglybtc-preview-smoke'],*methods,'-o',str(out/'wayland-smoke.xml')+',junitxml'],timeout=60)
 names=s.passed_xml(out/'wayland-smoke.xml');assert set(methods).issubset({x.rsplit('::',1)[-1].removesuffix('()') for x in names})
 text=runner.run('launcher-units',['python3','-B',N/'preview/native/packaging/test_preview.py'],timeout=60).read_text();count=re.search(r'Ran (\d+) tests?',text);assert count and int(count.group(1))>0
 assert s.source_snapshot(source)==before and s.images(paths)==current,'Source/image changed during check'
 assert s.sha(N/'dist/BUILD_MANIFEST.json')==result['dist_manifest_before'] and s.sha(binding['artifact'])==result['provider_sha256']
 combined=sorted([*fresh,*reused]);expected={'queue-policy','network-policy','mainline-dht'}| (set() if a.lane=='clang' else {'mainline-single-http','mainline-single-udp','mainline-single-tracker-owner','owned-tor-child-lifecycle'})
 assert set(combined)==expected and len(combined)==len(expected)
 if a.lane!='clang':
  artifact=R/'build/dependencies/tor-owned-0.4.9.11/runtime-fixture-artifact.json';tor=s.load(artifact)
  assert s.sha(artifact)=='25f0d75d2a2cc482b3e912457099ee666622444a0ae714151257844ca5987356'
  for key in ('launcher','tor_artifact'):assert s.sha(tor[key]['path'])==tor[key]['sha256']
  env.update(BIGLYBTC_OWNED_TOR_EXECUTABLE=tor['launcher']['path'],BIGLYBTC_OWNED_TOR_RUNTIME_ARTIFACT=str(artifact),BIGLYBTC_OWNED_TOR_RUNTIME_ARTIFACT_SHA256=s.sha(artifact),BIGLYBTC_OWNED_TOR_RESULT=str(out/'real-tor-observed.json'))
  runner.run('real-offline-tor',[paths['biglybtc-tor-owned-child-tests'],'--real-offline-tor','-o',str(out/'real-offline-tor.xml')+',junitxml'],timeout=60)
  real=s.passed_xml(out/'real-offline-tor.xml');assert len(real)==3 and any('extractedTorAuthenticatesOfflineThenStopsAndCancelsAnotherActualChild' in x for x in real)
  assert s.load(out/'real-tor-observed.json')['status']=='PASS_REAL_EXTRACTED_OFFLINE_TOR'
  result.update(real_tor_xml_sha256=s.sha(out/'real-offline-tor.xml'),real_tor_observed_sha256=s.sha(out/'real-tor-observed.json'))
 assert s.source_snapshot(source)==before and s.images(paths)==current,'Source/image changed during final subjects'
 assert s.sha(N/'dist/BUILD_MANIFEST.json')==result['dist_manifest_before'] and s.sha(binding['artifact'])==result['provider_sha256']
 result.update(status='PASS_FOCUSED_NETWORK_UI_REPAIR_IDENTICAL_OTHER_IMAGES',suite_names=actual,actual_suites=len(fresh),identical_image_passed_suites=reused,combined_covered_suites=combined,prior_source_commit=old['source_commit'],all_other_source_bytes_unchanged=True,all_other_images_unchanged=True,source_unchanged=True,images_unchanged=True,wayland_smoke_methods=methods,launcher_unit_tests=int(count.group(1)),tests_xml_sha256=s.sha(out/'tests.xml'),full_ctest_log_sha256=s.sha(out/'ctest-full.log'),wayland_smoke_xml_sha256=s.sha(out/'wayland-smoke.xml'))
except BaseException as e:result['failure']=repr(e)
finally:
 result['commands_sha256']=s.sha(out/'commands.json') if (out/'commands.json').exists() else None;s.write(out/'result.json',result)
print(result['status'],out/'result.json',flush=True)
raise SystemExit(0 if result['status'].startswith('PASS_') else 1)
