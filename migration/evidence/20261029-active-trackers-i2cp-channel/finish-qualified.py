#!/usr/bin/env python3
"""Root-only closeout of active URL counts + initial I2CP channel. No reruns."""
import argparse, ctypes, datetime, hashlib, importlib.util, json, os, pathlib, re, shutil, subprocess
import xml.etree.ElementTree as ET
R=pathlib.Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';H=pathlib.Path(__file__).parent
E=pathlib.Path('migration/evidence/20261029-active-trackers-i2cp-channel');SOURCE='82ef47fb32c30f57ebc9e135bcbffb00b4f7660f';IMPLEMENTATION='b998d3076cd34513ae5459adb7de03553c0b1844'
OLD_DIST='ac09f0dc9de3527e653e06b86e1c0d55d227f378593b0decc5233470f1cd2e69'
OLD_LOCK='5676f2ecd3b0071337c7e6047e63024e74e6f5b1ce55703de26b5e277cbd61d2'
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def load(p):return json.loads(pathlib.Path(p).read_bytes())
def write(p,v):pathlib.Path(p).write_text(json.dumps(v,indent=2)+'\n')
PINS={H/'run-r2.py':'dd7b6766b07cf060a5e04967b8b5af070a5abc71984a5a5b1cbfea9a50aab754',H/'root-adoption-r2.json':'916cc110ce0653a23593068680cc560866e50a49894765119ec748e70055e345',H/'r2-source-review.json':'489f0a839c265dadbb0db61c352f73ca3820c6e7c8c3ee7de0a4195cca34ee03',H/'private-focused/run-qualified.py':'437b01790b3227a69d1ed4886a528143b4326f7362c72f3cd77aed0dbaf7671b'}
ACTIVE=R/'build/mainline-active-tracker-urls-20261029/amendments/sole-authority/amendments/single-tier-udp'
CHANNEL=R/'build/i2cp-initial-channel-20261029';CHILD=CHANNEL/'amendments/date-commit-deadline'
PINS.update({ACTIVE/'manifest.json':'7c5f28c2908b2566554097cdd69861557ceef64d399e32d200341fbff19b85e8',R/'build/mainline-active-tracker-urls-20261029-review/single-tier-udp-review.json':'36fc38659d74265b7e7a1ca072040c2c076f3d522fe4d5619a7bb639b799b314',CHILD/'manifest.json':'451f00d89949320c7b45723b4747e5e0f59f0a08ffc43823264d3eba3fde33b1',CHANNEL/'manifest.json':'6548f61e84f636d1fe487414f19c1024ce2ee002c0c92e13a2305f07e7a129a4',R/'build/i2cp-initial-channel-20261029-review/manifest.json':'339d2154c910fbce670b637a6b96241d87581fb3c43b5ad64d23ca31ec20406b',CHANNEL/'fixture-origins.json':'5f599aec9b941c06a79e4106a3240f9149f9f99093c8d370c8fe49d9a44485ad',R/'build/mixed-i2cp-followup-20261029/offline-wrapper-r1/result.json':'2ce6033daf639a4b0ece0e52ba84baca495ed35366bb7ef3d3af1b23e348891f',R/'migration/evidence/20261029-mixed-i2cp/verification.json':'517ebcd7c1505ed9bd8cf50d96fcde8fcef03832d200f375b0d7c6307011e172'})

FIX=H/'amendments/actual-fixture-boundaries';FIX_REVIEW=H/'amendments/actual-fixture-boundaries-review'
PINS.update({FIX/'manifest.json':'310e90c3374362304c0bcff1c0278e6b7dfce392d4166132585b21130bceb184',FIX_REVIEW/'manifest.json':'db5f4276e5a7dbdb1b3d289bf4cc578435cddffe936a1003fe065c3359789515',H/'finish.py':'e94fd7435133cc37a03777690bdde4efae32fbdfb150c21345001c18581306f3',H/'finish-manifest.json':'3ef4e1ad23781c267fd99658bd0bf991158bf38c27982714cfd88e151fc71c84',H/'root-adoption.json':'0ab9e6e2daecdc2708112d4c461ca15aaf7fee9cb8fe0e46dfbb3f9d4312da42',H/'run.py':'954e54a067f540387ae56331ab84170e67f541dd2f177416f695f08789ec9ed9',H/'run-source-review.json':'337700dfd881403dc9652ebb4a2ed20b582096813c0d13123f1e17240ca4a4cc',H/'private-focused/run.py':'19979009119fcb691144224be4602aee070094a0695cb24daa01d93c76af2875',H/'finish-r2-failed-history.json':'5a27eb022fd596f74b5db3d30d0f0ccdfba2e8109b828ea78ef143e546f288cb'})

PINS.update({H/'qualify-r2.py':'d19745bf45082ed2abd76abbb15576a4f4ae34385ef7a364e2df6dbf77ae37c8',H/'amendments/qt-junit-verifier-review/manifest.json':'33316f4cbb4be82e9ef1bfc2538a196338dd7d7ab1f8e4169a5a3fee59fe2397'})

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--private-result',type=pathlib.Path,required=True);p.add_argument('--private-result-sha256',required=True);p.add_argument('--selection-plan',type=pathlib.Path,required=True);p.add_argument('--selection-plan-sha256',required=True);a=p.parse_args();a.private_result=a.private_result.resolve();a.selection_plan=a.selection_plan.resolve()
 for path,pin in PINS.items():assert sha(path)==pin,'Frozen input changed '+str(path)
 assert sha(a.private_result)==a.private_result_sha256 and sha(a.selection_plan)==a.selection_plan_sha256
 spec=importlib.util.spec_from_file_location('private_active',H/'private-focused/run-qualified.py');v=importlib.util.module_from_spec(spec);spec.loader.exec_module(v)
 for path,pin in v.PINS.items():assert sha(path)==pin
 spec=importlib.util.spec_from_file_location('support',v.SUPPORT);s=importlib.util.module_from_spec(spec);spec.loader.exec_module(s)
 source=s.source_snapshot(SOURCE);adoption=load(H/'root-adoption-r2.json');assert adoption['source_commit']==SOURCE and adoption['source_path_count']==len(adoption['source_paths'])==20 and adoption['all_twenty_working_and_git_afterimages_verified']
 for name,row in adoption['source_paths'].items():assert sha(N/name)==row['after_sha256']
 amendment=load(FIX/'manifest.json');leaf='preview/native/tests/managed_queue_tests.cpp';assert set(amendment['source_paths'])=={leaf} and amendment['native_source_basis']==IMPLEMENTATION and not amendment['production_changed']
 fixrow=amendment['source_paths'][leaf]
 for name,pin in amendment['artifacts'].items():assert sha(FIX/name)==pin
 for key in ('before','after'):assert sha(fixrow[key+'_path'])==fixrow[key+'_sha256']
 prior_adoption=load(H/'root-adoption.json');assert prior_adoption['source_commit']==IMPLEMENTATION and set(prior_adoption['source_paths'])==set(adoption['source_paths'])
 assert adoption['qualification_parent_commit']==IMPLEMENTATION and adoption['test_only_amendment_manifest_sha256']==sha(FIX/'manifest.json')
 assert {name for name in adoption['source_paths'] if adoption['source_paths'][name]!=prior_adoption['source_paths'][name]}=={leaf}
 git=lambda *args:subprocess.check_output(['git',*args],cwd=N,env=dict(os.environ,GIT_NO_LAZY_FETCH='1',GIT_OPTIONAL_LOCKS='0'))
 assert git('diff','--name-only',IMPLEMENTATION,SOURCE,'--','preview/native').decode().splitlines()==[leaf]
 assert hashlib.sha256(git('show',IMPLEMENTATION+':'+leaf)).hexdigest()==fixrow['before_sha256'] and hashlib.sha256(git('show',SOURCE+':'+leaf)).hexdigest()==fixrow['after_sha256']
 for name,row in load(ACTIVE/'manifest.json')['source_paths'].items():
  if name==leaf:assert row['after_sha256']==fixrow['before_sha256'] and adoption['source_paths'][name]['after_sha256']==fixrow['after_sha256']
  else:assert adoption['source_paths'][name]['after_sha256']==row['after_sha256']
 for name,row in load(CHILD/'manifest.json')['source_sha256'].items():assert adoption['source_paths'][name]['after_sha256']==row['after_sha256']
 for item in adoption['source_inputs']:assert sha(item['path'])==item['sha256']
 selection=load(a.selection_plan);assert selection['status']=='ROOT_FROZEN_PRIVATE_SELECTIONS' and selection['source_commit']==SOURCE and selection['source_receipt_sha256']==sha(H/'root-adoption-r2.json')
 for item in selection['source_capsules']:assert sha(item['path'])==item['sha256']
 rows=selection['selections'];assert [row['role'] for row in rows]==['managed_multi','udp_current','channel_full','sole_https']
 assert rows[0]['target']==rows[1]['target']=='biglybtc-managed-queue-tests' and rows[0]['selector']=='realManagedOwnerExposesUrlFactsButNeverClaimsAMultiUrlBestResponse:live-DHT-no-active-selection'
 assert rows[1]['selector'] in ('actualSingleTierUdpFailoverPublishesCurrentCountsWithoutANewScrape:DHT-disabled','actualSingleTierUdpFailoverPublishesCurrentCountsWithoutANewScrape:DHT-live-passive-only')
 assert rows[2]['target']=='biglybtc-i2cp-initial-channel-tests' and rows[2]['selector'] is None and rows[3]['target']=='biglybtc-mainline-single-http-tests' and rows[3]['selector']==v.HTTPS_SELECTOR
 assert all(row['expected_names'] and len(row['expected_names'])==len(set(row['expected_names'])) for row in rows)
 private=load(a.private_result);assert private['status']=='PASS_PRIVATE_ACTIVE_TRACKERS_AND_I2CP_CHANNEL' and private['source_commit']==SOURCE and private['source_unchanged'] and not private['builds_executed'] and not private['promotion_executed'] and not private['historical_runtime_passes_pooled']
 assert private['selection_plan_sha256']==a.selection_plan_sha256 and len(private['actual_tests'])==4
 plan=load(v.PLAN);provider=plan['provider']
 for key in ('manifest','record','approval','review'):assert sha(provider[key]['path'])==provider[key]['sha256']
 artifact=pathlib.Path(load(provider['manifest']['path'])['artifact']);assert sha(artifact)==private['provider_sha256']==provider['artifact_sha256']
 lanes={};images={}
 for lane in ('gcc','clang','san'):
  run=H/'runs'/(lane+'-r2');d=load(run/'result-qualified.json');wanted={'queue-policy','managed-queue','i2cp-initial-channel'}
  raw=load(run/'result.json');assert raw['status']=='FAIL' and raw['failure']=='AssertionError()' and d['original_verification_result_sha256']==sha(run/'result.json')
  assert d['qualification_verifier_sha256']==sha(H/'qualify-r2.py') and not d['commands_reexecuted']
  assert d['status']=='PASS_ACTIVE_CHANNEL_COHORT' and d['lane']==lane and d['source_commit']==SOURCE and d['source_unchanged'] and d['images_unchanged']
  assert d['actual_suites']==3 and set(d['suite_names'])==wanted and d['compile_only_suites']==['mainline-single-tracker-owner']
  assert d['runner_sha256']==sha(H/'run-r2.py') and d['source_receipt_sha256']==sha(H/'root-adoption-r2.json') and load(run/'source-before.json')==source
  authority=private['focused_results'][lane];assert authority['sha256']==sha(run/'result-qualified.json') and pathlib.Path(authority['path']).resolve()==(run/'result-qualified.json').resolve()
  assert authority['images_sha256']==sha(run/'images-before.json') and authority['source_before_sha256']==sha(run/'source-before.json')
  assert sha(run/'commands.json')==d['commands_sha256'];commands=load(run/'commands.json');assert [x['name'] for x in commands]==['build','http-loader','tests','sole-https-regression']
  for command in commands:assert command.get('exit')==0 and 'exception' not in command and sha(command['log'])==command['log_sha256']
  assert sha(run/'tests.xml')==d['tests_xml_sha256'];s.passed_xml(run/'tests.xml',wanted)
  assert sha(run/'ctest-full.log')==d['full_ctest_log_sha256'] and not re.search(r'^(?:SKIP\s*:|FAIL!)',(run/'ctest-full.log').read_text(),re.M)
  images[lane]=load(run/'images-before.json');assert len(images[lane])==8 and set(images[lane])==set(d['compiled_targets'])=={'biglybtc-mainline-single-http-tests','biglybtc-managed-queue-tests','biglybtc-queue-policy-tests','biglybtc-mainline-single-tracker-owner-tests','biglybtc-i2cp-initial-channel-tests','BiglyBTC','BiglyBTC-headless','biglybtc-preview-smoke'};v.actual_suite_commands(run/'ctest-full.log',wanted,images[lane])
  for image in images[lane].values():assert sha(image['path'])==image['sha256'] and pathlib.Path(image['path']).stat().st_size==image['bytes']
  assert d['sole_https_selector']==v.HTTPS_SELECTOR and sha(run/'sole-https-regression.xml')==d['sole_https_xml_sha256']
  names=s.passed_xml(run/'sole-https-regression.xml');assert len(names)==3 and {x.rsplit('::',1)[-1].removesuffix('()') for x in names}=={'initTestCase','cleanupTestCase','directAdmittedTrackerUsesActualAnnounceOrSavedTrustWithoutMainline(private-HTTPS-saved-JKS-regular-scrape)'}
  assert commands[3]['command']==['nice','-n','5',images[lane]['biglybtc-mainline-single-http-tests']['path'],v.HTTPS_SELECTOR,'-o',str(run/'sole-https-regression.txt')+',txt','-o',str(run/'sole-https-regression.xml')+',junitxml']
  assert all(row.get('status','run' if name=='sole-https-regression.xml' else None)=='run' for name in ('tests.xml','sole-https-regression.xml') for row in ET.parse(run/name).getroot().iter('testcase'))
  assert not re.search(r'^(?:SKIP\s*:|FAIL!)',(run/'sole-https-regression.txt').read_text(),re.M)
  assert d['provider_sha256']==provider['artifact_sha256'];lanes[lane]={k:d[k] for k in ('actual_suites','suite_names','compile_only_suites','sanitizer_environment')};lanes[lane]['separate_https_selector']=v.HTTPS_SELECTOR
 assert set(private['copied_images'])=={'biglybtc-managed-queue-tests','biglybtc-i2cp-initial-channel-tests','biglybtc-mainline-single-http-tests'}
 for target,image in private['copied_images'].items():assert image==images['gcc'][target]
 for i,(expected,actual) in enumerate(zip(rows,private['actual_tests'],strict=True),1):
  run=a.private_result.parent;assert actual['target']==expected['target'] and actual['selector']==expected['selector'] and actual['role']==expected['role'] and actual['exit']==0 and sha(run/f'selected-{i}.xml')==actual['xml_sha256']
  names=s.passed_xml(run/f'selected-{i}.xml');assert len(names)==len(expected['expected_names'])+2 and {x.rsplit('::',1)[-1].removesuffix('()') for x in names}=={'initTestCase','cleanupTestCase',*expected['expected_names']}
  assert names==actual['case_names'] and all(x.get('status','run')=='run' for x in ET.parse(run/f'selected-{i}.xml').getroot().iter('testcase'))
  for path,pin in actual['initialized_images_sha256'].items():assert sha(path)==pin
 assert sha(a.private_result.parent/'commands.json')==private['commands_sha256']
 private_commands=load(a.private_result.parent/'commands.json');assert [x['name'] for x in private_commands]==['stage',*['loader-'+x for x in sorted(private['copied_images'])],*['selected-'+str(x) for x in range(1,5)]]
 for command in private_commands:assert command.get('exit')==0 and 'exception' not in command and sha(command['log'])==command['log_sha256']
 # Historical Java origin proves the seven byte vectors, never a new b998 replay.
 origins=load(CHANNEL/'fixture-origins.json');history=load(origins['receipt']);assert sha(origins['receipt'])==origins['receipt_sha256'] and history['source_commit']==origins['source_commit']=='ef494c64add015ea84df93ebcfbff1c7a38fa10b' and history['status']=='PASS_ACTUAL_I2CP_INITIAL_SESSION_OFFLINE' and history['cases_per_reference']==12
 assert len(origins['vectors'])==7 and sum(row['bytes'] for row in origins['vectors'])==78
 for row in origins['vectors']:
  for path in (N/row['path'],pathlib.Path(row['official_origin']),pathlib.Path(row['modified_origin'])):assert sha(path)==row['sha256'] and path.stat().st_size==row['bytes']
  assert adoption['source_paths'][row['path']]['after_sha256']==row['sha256']
 failed_history=load(H/'finish-r2-failed-history.json');assert failed_history['status']=='ACTUAL_FAILED_R1_INPUTS_NO_FINAL_PASS_CREDIT' and failed_history['source_commit']==IMPLEMENTATION
 for path,pin in failed_history['paths_sha256'].items():assert sha(H/path)==pin
 historical={}
 for lane in ('gcc','clang','san'):
  run=H/'runs'/(lane+'-r1');failed=load(run/'result.json');assert failed['status']=='FAIL' and failed['source_commit']==IMPLEMENTATION and failed['lane']==lane and failed['runner_sha256']==sha(H/'run.py') and failed['source_receipt_sha256']==sha(H/'root-adoption.json')
  assert sha(run/'commands.json')==failed['commands_sha256'];commands=load(run/'commands.json');assert [(row['name'],row.get('exit')) for row in commands]==[('build',0),('http-loader',0),('tests',8)]
  for row in commands:assert sha(row['log'])==row['log_sha256']
  xml=list(ET.parse(run/'tests.xml').getroot().iter('testcase'));assert len(xml)==3 and {row.get('name') for row in xml}=={'managed-queue','queue-policy','i2cp-initial-channel'}
  assert {row.get('name') for row in xml if list(row.iter('failure'))}=={'managed-queue'} and not any(list(row.iter('skipped')) for row in xml)
  assert len(re.findall(r'^FAIL!  : ManagedQueueTests::',(run/'ctest-full.log').read_text(),re.M))==5
  old_source=load(run/'source-before.json');assert old_source['source_commit']==IMPLEMENTATION
  assert set(old_source['files_sha256'])==set(source['files_sha256']) and {name for name in source['files_sha256'] if source['files_sha256'][name]!=old_source['files_sha256'][name]}=={leaf}
  assert old_source['files_sha256'][leaf]==fixrow['before_sha256'] and source['files_sha256'][leaf]==fixrow['after_sha256']
  historical[lane]={'status':'FAIL','source_commit':IMPLEMENTATION,'result_sha256':sha(run/'result.json'),'tests_xml_sha256':sha(run/'tests.xml'),'failed_suite':'managed-queue','failed_qt_rows':5,'credited_for_final_source':False}
 out=R/E;assert not out.exists() and not (N/E).exists() and not (H/'promotion.json').exists()
 old_lock=(R/'migration/upstreams.lock.json').read_bytes();assert hashlib.sha256(old_lock).hexdigest()==OLD_LOCK and old_lock==(N/'migration/upstreams.lock.json').read_bytes();lock=json.loads(old_lock)
 assert len(lock['upstreams'])==20 and len(lock['components'])==108 and len(lock['external_dependencies'])==22
 before=json.loads(old_lock);changed={'engine.mainline_queued_scrape','engine.tracker_scrape_observations','services.i2cp_initial_session'}
 for c in lock['components']:
  if c['id'] not in changed:continue
  assert c['state']=='in_progress' and all(row['first_ported_commit'] is None and row['integrated_through_commit'] is None for row in c['source_coverage'])
  c['native_commits'].extend([{'repository':'https://github.com/d00rsfan/BiglyBTC.git','commit':IMPLEMENTATION,'purpose':'integration'},{'repository':'https://github.com/d00rsfan/BiglyBTC.git','commit':SOURCE,'purpose':'tests'}]);c['review_records'].append((E/'REVIEW.md').as_posix());c['evidence'].append((E/'verification.json').as_posix())
  targets=['preview/native/services/i2cp_channel/channel.hpp','preview/native/services/i2cp_channel/channel.cpp','preview/native/tests/i2cp_initial_channel_tests.cpp'] if c['id']=='services.i2cp_initial_session' else ['preview/native/engine/managed_tracker_responses.hpp','preview/native/engine/persistent_session.hpp','preview/native/engine/persistent_session.cpp']
  for target in targets:
   if target not in c['target_paths']:c['target_paths'].append(target)
  for coverage in c['source_coverage']:
   additions=['core/java/src/net/i2p/client/impl/SetDateMessageHandler.java','core/java/src/net/i2p/client/impl/DisconnectMessageHandler.java'] if coverage['upstream_id']=='i2p-java' else ['core/src/com/biglybt/core/tracker/client/impl/TRTrackerAnnouncerMuxer.java','core/src/com/biglybt/core/tracker/client/impl/bt/TrackerChecker.java'] if coverage['upstream_id']=='BiglyBT' else []
   for path in additions:
    if path not in coverage['source_paths']:coverage['source_paths'].append(path)
 assert lock['upstreams']==before['upstreams'] and lock['external_dependencies']==before['external_dependencies']
 assert {c['id'] for c,old in zip(lock['components'],before['components'],strict=True) if c!=old}==changed
 for c,old in zip(lock['components'],before['components'],strict=True):
  assert c['state']==old['state']
  for row,prior in zip(c['source_coverage'],old['source_coverage'],strict=True):
   for key in ('upstream_id','baseline_commit','first_ported_commit','integrated_through_commit','last_reviewed_commit'):assert row[key]==prior[key]
 candidate=pathlib.Path(private['candidate']);dist=N/'dist';assert candidate.is_relative_to(a.private_result.parent) and sha(candidate/'BUILD_MANIFEST.json')==private['candidate_manifest_sha256'];new=v.bundle(candidate);old=v.bundle(dist);old_manifest=(dist/'BUILD_MANIFEST.json').read_bytes();assert sha(dist/'BUILD_MANIFEST.json')==private['old_dist_unchanged']==OLD_DIST
 removed=[]
 for target,image in private['copied_images'].items():
  path=a.private_result.parent/target;assert path.is_file() and not path.is_symlink() and sha(path)==image['sha256'];removed.append({'path':str(path),'sha256':sha(path)})
 support=a.private_result.parent/'test-support';assert support.is_dir() and not support.is_symlink() and {x.name for x in support.iterdir()}=={'libQt6Test.so.6'}
 for maps in pathlib.Path('/proc').glob('[0-9]*/maps'):
  try:text=maps.read_text()
  except (OSError,PermissionError):continue
  assert all(str(path)+'/' not in text for path in (dist,candidate,support)) and not any(row['path'] in text for row in removed),'A private/current image is still mapped'
 assert s.source_snapshot(SOURCE)==source
 libc=ctypes.CDLL(None,use_errno=True);exchange=libc.renameat2;exchange.argtypes=[ctypes.c_int,ctypes.c_char_p,ctypes.c_int,ctypes.c_char_p,ctypes.c_uint]
 assert exchange(-100,os.fsencode(dist),-100,os.fsencode(candidate),2)==0,os.strerror(ctypes.get_errno());assert v.bundle(dist)==new and v.bundle(candidate)==old;shutil.rmtree(candidate)
 for row in removed:pathlib.Path(row['path']).unlink()
 shutil.rmtree(support)
 promotion={'status':'PROMOTED_ONE_CHECKED_DEVELOPMENT_DIST','source_commit':SOURCE,'manifest_sha256':sha(dist/'BUILD_MANIFEST.json'),'leaf_count':len(new['files_sha256']),'private_result_sha256':sha(a.private_result),'previous_dist_manifest_sha256':OLD_DIST,'previous_dist_removed':True,'previous_dist_retained':False,'atomic_exchange':True,'copied_test_images_removed':removed,'test_support_removed':True,'created_utc':datetime.datetime.now(datetime.timezone.utc).isoformat()};write(H/'promotion.json',promotion)
 out.mkdir(parents=True,exist_ok=False);copied={}
 def copy(path,name):
  path=pathlib.Path(path);assert path.is_file() and not path.is_symlink() and path.stat().st_size<=8*1024*1024
  dest=out/name;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(path,dest);copied[name]=sha(dest)
 for name in ('root-adoption-r2.json','run-r2.py','r2-source-review.json','promotion.json','finish-r2.py','finish-r2-failed-history.json'):copy(H/name,name)
 for name in ('finish-qualified.py','qualify-r2.py'):copy(H/name,name)
 for name in ('manifest.json','review.json'):copy(H/'amendments/qt-junit-verifier-review'/name,'verification-correction/'+name)
 for name in ('root-adoption.json','run.py','run-source-review.json','finish.py','finish-manifest.json'):copy(H/name,'historical-r1/recipes/'+name)
 copy(H/'private-focused/run.py','historical-r1/recipes/private-run.py')
 for directory,label in ((FIX,'test-fix'),(FIX_REVIEW,'test-fix-review')):
  for path in directory.rglob('*'):
   if path.is_file():copy(path,'source/'+label+'/'+str(path.relative_to(directory)))
 for path in failed_history['paths_sha256']:copy(H/path,'historical-r1/'+path)
 copy(a.selection_plan,'private/selection-plan.json')
 for name in ('run-r2.py','run-qualified.py','draft-source-review.json'):copy(H/'private-focused'/name,'private/'+name)
 for capsule,label in [(ACTIVE,'active'),(CHILD,'channel-final'),(CHANNEL,'channel-parent')]:
  copy(capsule/'manifest.json','source/'+label+'/manifest.json');copy(capsule/('ADOPTION.md' if label.startswith('channel') else 'ADOPTION.txt'),'source/'+label+'/ADOPTION.txt')
 for path,label in [(R/'build/mainline-active-tracker-urls-20261029-review/single-tier-udp-review.json','active'),(R/'build/i2cp-initial-channel-20261029-review/manifest.json','channel-manifest'),(R/'build/i2cp-initial-channel-20261029-review/review.json','channel')]:copy(path,'source/'+label+'-review.json')
 copy(ACTIVE/'original-source-pins.json','source/active/source-pins.json');copy(ACTIVE/'changes.patch','source/active/changes.patch');copy(CHANNEL/'source-pins.json','source/channel/source-pins.json');copy(CHILD/'parent-delta.patch','source/channel/deadline-correction.patch');copy(CHANNEL/'fixture-origins.json','source/channel/fixture-origins.json')
 copy(origins['receipt'],'historical-M/offline-result.json');copy(R/'migration/evidence/20261029-mixed-i2cp/verification.json','historical-M/verification.json')
 for row in origins['vectors']:copy(N/row['path'],'source/channel/vectors/'+pathlib.Path(row['path']).name)
 for lane in lanes:
  for name in ('result.json','result-qualified.json','commands.json','images-before.json','source-before.json','tests.xml','ctest-full.log','build.log','http-loader.log','tests.log','sole-https-regression.log','sole-https-regression.xml','sole-https-regression.txt'):copy(H/'runs'/(lane+'-r2')/name,'runs/'+lane+'-r2/'+name)
 for path in a.private_result.parent.iterdir():
  if path.is_file() and path.suffix in ('.json','.xml','.txt','.log'):copy(path,'private/run/'+path.name)
 copy(dist/'BUILD_MANIFEST.json','dist-manifest.json');(out/'previous-dist-manifest.json').write_bytes(old_manifest);copied['previous-dist-manifest.json']=sha(out/'previous-dist-manifest.json')
 write(out/'verification.json',{'status':'PASS_BOUNDED_ACTIVE_TRACKER_COUNTS_AND_I2CP_INITIAL_CHANNEL','source_commit':SOURCE,'implementation_source_commit':IMPLEMENTATION,'test_only_qualification_commit':SOURCE,'test_amendment_manifest_sha256':sha(FIX/'manifest.json'),'source_path_count':20,'failed_r1_history':historical,'r2_report_correction':'All four commands passed; raw result preserves the Qt JUnit status-field verification error. result-qualified.json revalidates unchanged current source/images/outputs without rerunning commands.','lanes':lanes,'actual_private_commands':4,'private_channel_runnable_cases':len(rows[2]['expected_names']),'private_result_sha256':sha(a.private_result),'offline_Java_replayed':False,'fixture_origins':{'source_commit':origins['source_commit'],'receipt_sha256':origins['receipt_sha256'],'historical_evidence_only':True,'retained_vectors':7,'retained_bytes':78},'provider_rebuilt':False,'provider_sha256':private['provider_sha256'],'dist_manifest_sha256':promotion['manifest_sha256'],'prior_dist_retained':False,'real_profile_used':False,'public_network_activated':False,'whole_feature_or_performance_parity_claimed':False,'artifacts_sha256':copied})
 (out/'REVIEW.md').write_text(f'''The raw r2 reports retain a verifier-only failure: direct Qt JUnit omits per-case status. All four actual r2 commands exited zero; the corrected source-reviewed verifier requires strict CTest status and exact clean Qt summary/cases, then rechecks unchanged source/images/logs. No build or test command was rerun for this metadata correction.

# Active URL counts and initial I2CP channel

Implementation {IMPLEMENTATION}, qualified at test-only successor {SOURCE}, twenty exact adopted paths: activeURL/currentUDP8, initialI2CPchannel10 and CMake2. The three initial b998 r1 whole invocations failed five managed fixture rows each; their logs/XML/image maps/commands/results are retained as failed history, with no final pass credit. The reviewed one-leaf test amendment validates exact BEP41 URLData, restores post-prepare mapping and requires exact checkpoint Conflict after deliberate source edits. Production and all deadlines are unchanged. Each r2 lane built the same eight products and freshly passed exactly three CTest suites (managed-queue, queue-policy, i2cp-initial-channel) plus one separate sole HTTPS/JKS row. Full tracker-owner is compile-only on all lanes; full HTTP and old I2CP codec/Tor/provider/Java were not rerun or pooled. Private qualification ran four actual commands: managed multiURL, current single-tierUDP, whole channel, sole HTTPS. Three copied test ELFs and QtTest support were removed. One private candidate atomically replaced priorac09 dist; old full tree was deleted immediately and only manifest/proof remains.

The existing fixed256 callback ring retains up to8 immutable admitted URL responses. Owned values and current token/loss/retirement guards do not invent a best response. Supported active current selection is exactly one original tier of2..8 numericUDP URLs: typed actual dispatch handle/protocol/local endpoint determines current URL; pre-pop ring cut withholds post-collection terminals until later dispatch collection. Same fresh original metadata/permissions/captured group gates bind passive selected counts into the existing library view. Actual owned first-announce failure caused real LT failover to the other URL and selected7/8; the fixture forbids every SCRAPE. No new tracker request, queue facts/epoch, DHT/PORT/peer permission or timer is added. General helper grouping/status/HTTP redirect selection remains open.

Initial I2CP is an explicit numeric-loopback QtTCP prerequisite: original0x2a greeting/no-authGetDate and bounded SetDate observation. The actual owned channel covers fragmentation, disconnect/refusal/EOF/timeout, stale cancellation/restart and reentrant deletion. Final deadline/resource correction is retained. It never creates/accepts an I2CP session, adjusts Clock, selects/starts a router, reads a profile, reconnects automatically or activates an app backend. Remote/TLS/authentication/CreateSession/config/identity/lease/data/lookup remain open. The seven78-byte Java vectors come from the separately qualified earlierM sourceef494c64; its actual receipt/class/artifact provenance is historical, not a new replay or new-source Java equivalence. No public/whole-feature/performance completion is claimed. Existing108 components/20 sources/22 dependencies and all baselines remain; only three in-progress records are extended, integration coverage fields remain null.
''')
 task='''# P07-02b bounded I2CP initial prerequisite

State: in_progress; feature F06.i2cp_initial_session. Pure codec implementation ef494c64add015ea84df93ebcfbff1c7a38fa10b remains qualified separately in ../evidence/20261029-mixed-i2cp/verification.json. Initial loopback QtTCP channel implementation b998d3076cd34513ae5459adb7de03553c0b1844, qualified at test-only successor 82ef47fb32c30f57ebc9e135bcbffb00b4f7660f, is recorded in ../evidence/20261029-active-trackers-i2cp-channel/verification.json. Source integration fields remain null.

Original i2p-java9134f808337b401e8e53c73734c81fab04280c9d and plugin context33d97eca67a9f332e3d4da7e7492ec54f4bec078 remain separate. Seven78-byte original message vectors retain actual official/modified artifact origins from the earlier twelve-case-per-reference offline comparison; no Java replay or whole modified-core equivalence is claimed here.

Support bounded5-byte framing/payload128KiB and typed no-auth initial messages. The new caller-owned channel sends0x2a/GetDate only to explicit numeric IPv4/IPv6 loopback, observes one nonzero SetDate and closes on unsupported/malformed input or disconnect. Absolute default30s deadline (caller1..30000ms), generation cancellation and queued reentrant notification guards apply. It does not change Clock or create/accept any session. Current actual three-lane channel tests and one private whole-channel command are distinct from old pure codec/offline evidence.

Authentication/properties, non-ASCII conversion, original malformed/trailer permissiveness, remote/TLS, CreateSession/config/signatures/lease sets/data/lookup, profile/router startup, application activation and full session lifecycle remain unsupported. All corresponding P07 completion/performance remains open.
'''
 shutil.copytree(out,N/E)
 prepared=[]
 for root in (R,N):
  (root/'migration/tasks/P07-02b-i2cp-initial-session.md').write_text(task)
  target=root/'migration/upstreams.lock.json';assert target.read_bytes()==old_lock;pending=target.with_name(target.name+'.active-channel-pending')
  with pending.open('x') as stream:stream.write(json.dumps(lock,indent=2)+'\n');stream.flush();os.fsync(stream.fileno())
  prepared.append((target,pending))
 for target,pending in prepared:assert target.read_bytes()==old_lock;os.replace(pending,target)
 for label,root in [('R',R),('N',N)]:
  log=H/('registry-validation-'+label+'.log')
  with log.open('wb') as stream:completed=subprocess.run(['python3','-B',root/'migration/tools/validate.py'],cwd=root,env=dict(os.environ,GIT_NO_LAZY_FETCH='1',GIT_OPTIONAL_LOCKS='0'),stdout=stream,stderr=subprocess.STDOUT)
  assert completed.returncode==0 and 'PASS registry: 20 sources, 108 components' in log.read_text(),'Registry validation failed '+label
  shutil.copyfile(log,out/log.name);shutil.copyfile(log,N/E/log.name)
 verification=load(out/'verification.json');verification['registry_validation_sha256']={label:sha(H/('registry-validation-'+label+'.log')) for label in ('R','N')};verification['unchanged_registry_counts']={'sources':20,'components':108,'external_dependencies':22};write(out/'verification.json',verification);write(N/E/'verification.json',verification)
 print('PROMOTED_AND_RECORDED',SOURCE,promotion['manifest_sha256'],'fresh3/3/3 plus separate HTTPS each, private4, historical vectors only;20/108/22 unchanged; priorac09 deleted')
if __name__=='__main__':main()
