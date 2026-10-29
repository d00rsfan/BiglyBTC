#!/usr/bin/env python3
"""Revalidate existing successful r2 executions with the actual Qt JUnit schema.

No compiler, test binary or distribution stager is executed. Original failed
verification receipts remain unchanged beside the new qualified receipts.
"""
import hashlib, importlib.util, json, pathlib, re
import xml.etree.ElementTree as ET

R=pathlib.Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';H=pathlib.Path(__file__).parent
SOURCE='82ef47fb32c30f57ebc9e135bcbffb00b4f7660f'
spec=importlib.util.spec_from_file_location('support',R/'build/mainline-scrape-20261028/recipe_support_followup.py')
s=importlib.util.module_from_spec(spec);spec.loader.exec_module(s)
runner_pin='dd7b6766b07cf060a5e04967b8b5af070a5abc71984a5a5b1cbfea9a50aab754'
assert s.sha(H/'run-r2.py')==runner_pin
adoption=s.load(H/'root-adoption-r2.json');assert adoption['source_commit']==SOURCE and adoption['source_path_count']==20
source=s.source_snapshot(SOURCE)
for name,row in adoption['source_paths'].items():assert s.sha(N/name)==row['after_sha256']
targets=['biglybtc-managed-queue-tests','biglybtc-queue-policy-tests','biglybtc-mainline-single-tracker-owner-tests',
 'biglybtc-mainline-single-http-tests','biglybtc-i2cp-initial-channel-tests','BiglyBTC','BiglyBTC-headless','biglybtc-preview-smoke']
suites=['queue-policy','managed-queue','i2cp-initial-channel']
selector='directAdmittedTrackerUsesActualAnnounceOrSavedTrustWithoutMainline:private-HTTPS-saved-JKS-regular-scrape'
expected={'initTestCase','cleanupTestCase','directAdmittedTrackerUsesActualAnnounceOrSavedTrustWithoutMainline(private-HTTPS-saved-JKS-regular-scrape)'}
qualified=[]
for lane in ('gcc','clang','san'):
 out=H/'runs'/(lane+'-r2');raw=out/'result.json';d=s.load(raw)
 assert d['status']=='FAIL' and d['failure']=='AssertionError()' and d['source_commit']==SOURCE and d['lane']==lane
 assert d['runner_sha256']==runner_pin and d['source_receipt_sha256']==s.sha(H/'root-adoption-r2.json')
 assert d['compiled_targets']==targets and d['compile_only_suites']==['mainline-single-tracker-owner']
 assert s.load(out/'source-before.json')==source
 commands=s.load(out/'commands.json');assert s.sha(out/'commands.json')==d['commands_sha256']
 assert [row['name'] for row in commands]==['build','http-loader','tests','sole-https-regression']
 for row in commands:assert row.get('exit')==0 and 'exception' not in row and s.sha(row['log'])==row['log_sha256']
 images=s.load(out/'images-before.json');assert set(images)==set(targets) and len(images)==8
 paths={name:pathlib.Path(row['path']) for name,row in images.items()};assert s.images(paths)==images
 assert commands[0]['command']==['nice','-n','5','cmake','--build',d['cache'],'--parallel','1','--target',*targets]
 assert commands[1]['command']==['ldd',str(paths['biglybtc-mainline-single-http-tests'])]
 assert commands[2]['command']==['nice','-n','5','ctest','--test-dir',d['cache'],'--parallel','1','-R','^('+'|'.join(suites)+')$','--output-on-failure','--output-junit',str(out/'tests.xml')]
 assert commands[3]['command']==['nice','-n','5',str(paths['biglybtc-mainline-single-http-tests']),selector,'-o',str(out/'sole-https-regression.txt')+',txt','-o',str(out/'sole-https-regression.xml')+',junitxml']
 actual=s.passed_xml(out/'tests.xml',suites)
 assert all(case.get('status')=='run' for case in ET.parse(out/'tests.xml').getroot().iter('testcase'))
 root=ET.parse(out/'sole-https-regression.xml').getroot()
 assert root.tag=='testsuite' and int(root.get('tests'))==3
 assert all(int(root.get(key))==0 for key in ('failures','errors','skipped'))
 names=s.passed_xml(out/'sole-https-regression.xml');assert len(names)==3 and set(names)==expected
 assert all(case.get('status','run')=='run' for case in root.iter('testcase'))
 for name in ('ctest-full.log','sole-https-regression.txt'):
  assert not re.search(r'^(?:SKIP\s*:|FAIL!)',(out/name).read_text(),re.M)
 linked=(out/'http-loader.log').read_text();match=re.search(r'libtorrent-rasterbar[^ ]* => (/.*?) \(',linked)
 assert match and s.sha(match.group(1))==d['provider_sha256']=='355f0c7bca7404c3199e85d4741a3734ac460c79d641a2539c049c5a8ad0c443'
 assert s.sha(N/'dist/BUILD_MANIFEST.json')==d['dist_manifest_before']=='ac09f0dc9de3527e653e06b86e1c0d55d227f378593b0decc5233470f1cd2e69'
 d.update(status='PASS_ACTIVE_CHANNEL_COHORT',suite_names=actual,actual_suites=3,
  full_ctest_log_sha256=s.sha(out/'ctest-full.log'),tests_xml_sha256=s.sha(out/'tests.xml'),
  sole_https_selector=selector,sole_https_xml_sha256=s.sha(out/'sole-https-regression.xml'),
  source_unchanged=True,images_unchanged=True,
  sanitizer_environment=({'ASAN_OPTIONS':'detect_leaks=1:halt_on_error=1','UBSAN_OPTIONS':'halt_on_error=1:print_stacktrace=1'} if lane=='san' else {}),
  sanitizer_configuration_basis='Executed immutable runner source; no separate process-environment capture claimed',
  original_verification_result_sha256=s.sha(raw),qualification_verifier_sha256=s.sha(__file__),
  verification_correction='CTest requires status=run; direct Qt JUnit permits absent status, with exact cases and zero summary/child failures/errors/skips',
  commands_reexecuted=False)
 d.pop('failure');dest=out/'result-qualified.json';assert not dest.exists();qualified.append((dest,d))
assert s.source_snapshot(SOURCE)==source
for path,value in qualified:
 s.write(path,value);print('QUALIFIED_EXISTING_R2',value['lane'],'3 suites + HTTPS; no commands rerun')
