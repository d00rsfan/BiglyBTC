#!/usr/bin/env python3
"""Root-only closeout; consume actual current gates, never rebuild or replay."""
import argparse,ctypes,importlib.util,json,os,pathlib,re,shutil,subprocess
R=pathlib.Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC'
H=pathlib.Path(__file__).resolve().parent;E=pathlib.Path('migration/evidence/20261029-contacts-enc4')
RETENTION_SHA='81025f4fa723ac8db8bbf67a8ea9c3a8673e5461ce5d2a07c9e7d49eb95208e0'
OLD='3c38e092d97a3fb4299ba964a720c9a490d43bf7f99c48292f7785945c88caed'
def load(p):return json.loads(pathlib.Path(p).read_text())
def module(p):
 spec=importlib.util.spec_from_file_location('qualified_private',p);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m

def main():
 p=argparse.ArgumentParser();p.add_argument('--attempt',required=True)
 for name in ('producer','adoption','private-recipe','offline-result','router-result'):p.add_argument('--'+name,required=True,type=pathlib.Path)
 a=p.parse_args();assert re.fullmatch(r'r[0-9]+',a.attempt)
 assert all(getattr(a,n).is_absolute() for n in ('producer','adoption','private_recipe','offline_result','router_result'))
 v=module(a.private_recipe);s=v.s;batch=v.bind_producer(a.producer)
 adoption_path=a.adoption;adoption=load(adoption_path);source=adoption['source_commit'];snapshot=s.source_snapshot(source)
 assert not (H/'promotion.json').exists() and not (R/E).exists() and not (N/E).exists()
 assert len(adoption['source_paths'])==adoption['source_path_count']
 for name,row in adoption['source_paths'].items():assert s.sha(N/name)==s.sha(row['after_path'])==row['after_sha256']
 def command_receipts(root,result,names=None):
  assert s.sha(root/'commands.json')==result['commands_sha256'];rows=load(root/'commands.json')
  if names is not None:assert [row['name'] for row in rows]==names
  for row in rows:assert row['exit']==0 and not row.get('exception') and s.sha(row['log'])==row['log_sha256']
 lanes={};images={}
 for lane in ('gcc','clang','san'):
  root=H/'runs'/(lane+'-'+a.attempt);d=load(root/'result.json');suites=batch.suites_for_lane(lane)
  assert d['status']=='PASS_ENC4_LEASESET_COHORT' and d['lane']==lane and d['source_commit']==source and d['source_receipt_sha256']==s.sha(adoption_path)
  assert d['actual_suites']==len(suites) and d['source_unchanged'] and d['images_unchanged'] and not d['historical_passes_pooled']
  assert d['runner_sha256']==s.sha(a.producer) and load(root/'source-before.json')==snapshot
  assert s.sha(root/'tests.xml')==d['tests_xml_sha256'] and s.sha(root/'ctest-full.log')==d['full_ctest_log_sha256']
  assert batch.base.check_xml(root/'tests.xml',suites)==d['suite_names']
  command_receipts(root,d,['configure','build','inventory','tests'])
  assert load(root/'source-after.json')==snapshot and load(root/'native-source-map.json')==snapshot['files_sha256']
  images[lane]=load(root/'images-before.json');assert load(root/'images-after.json')==images[lane]
  assert set(images[lane])==set(suites.values())|(set(batch.PRODUCTS) if lane=='gcc' else set())==set(d['compiled_targets'])
  for img in images[lane].values():assert s.sha(img['path'])==img['sha256'] and pathlib.Path(img['path']).stat().st_size==img['bytes']
  assert d['provider']==batch.provider_pins()
  lanes[lane]={'result_sha256':s.sha(root/'result.json'),'actual_suites':len(suites),'compiled_images':len(images[lane]),'sanitizer_environment':d['sanitizer_environment']}
 privatepath=H/'private-runs'/a.attempt/'result.json';private=load(privatepath)
 # Private recipe deliberately exposes the same contract as the previous bounded batch.
 assert private['status']=='PASS_PRIVATE_CONTACTS_ENC4' and private['source_commit']==source and private['source_unchanged'] and not private['builds_executed'] and not private['provider_rebuilt']
 assert private['runner_sha256']==s.sha(a.private_recipe) and private['candidate_manifest_sha256']==s.sha(pathlib.Path(private['candidate'])/'BUILD_MANIFEST.json')
 assert [t['suite'] for t in private['actual_tests']]==list(batch.suites_for_lane('gcc'))
 assert set(private['copied_images'])==set(batch.suites_for_lane('gcc').values())
 assert private['source_adoption_sha256']==s.sha(adoption_path) and private['producer_sha256']==s.sha(a.producer)
 assert len(private['actual_tests'])==len(batch.suites_for_lane('gcc')) and not private['historical_passes_pooled']
 assert set(private['lane_inputs'])=={'gcc','clang','san'}
 for lane,row in private['lane_inputs'].items():assert pathlib.Path(row['path']).resolve()==(H/'runs'/(lane+'-'+a.attempt)/'result.json').resolve() and row['sha256']==s.sha(row['path'])
 command_receipts(privatepath.parent,private)
 for target,img in private['copied_images'].items():
  assert img==images['gcc'][target] and s.sha(privatepath.parent/target)==img['sha256']
 for test in private['actual_tests']:
  assert test['exit']==0 and s.sha(privatepath.parent/(test['suite']+'.log'))==test['log_sha256']
  if 'xml_sha256' in test:
   xml=privatepath.parent/(test['suite']+'.xml');assert s.sha(xml)==test['xml_sha256'] and s.passed_xml(xml)==test['case_names']
  for name,pin in test['initialized_images_sha256'].items():assert s.sha(name)==pin
 offlinepath=a.offline_result;offline=load(offlinepath);offroot=offlinepath.parent
 assert offline['status']=='PASS_ACTUAL_ENC4_LEASESET_OBJECTS' and offline['source_commit']==source
 assert offline['gcc_result_sha256']==s.sha(H/'runs'/('gcc-'+a.attempt)/'result.json') and offline['runner_sha256']==s.sha(H/'offline.py')
 assert (offline['cases_per_reference_per_direction'],offline['java_to_native_frames_per_reference'],offline['native_to_java_frames_per_reference'],offline['changed_signature_negatives_per_reference'])==(4,8,4,1)
 for flag in ('source_unchanged','native_images_unchanged','provider_unchanged','classes_removed_after_hashing'):assert offline[flag] is True
 for flag in ('provider_rebuilt','old_java_replays','router_or_real_profile_claimed'):assert offline[flag] is False
 command_receipts(offroot,offline,['enc4-objects'])
 assert offline['inputs_sha256']==s.sha(offroot/'inputs.json') and offline['outputs_before_cleanup_sha256']==s.sha(offroot/'outputs-before-class-cleanup.json') and offline['tool_invocations_sha256']==s.sha(offroot/'tool-invocations.jsonl')
 oi=load(offroot/'inputs.json');assert oi['source_commit']==source and oi['native_images']==images['gcc'] and oi['provider']==batch.provider_pins()
 for name,pin in {**oi['files'],**oi['controlled_wrappers']}.items():assert s.sha(name)==pin['sha256'] and pathlib.Path(name).stat().st_size==pin['bytes']
 assert not (offroot/'vectors/classes').exists() and set(offline['helper_classes'])=={'LeaseSetEnc4Vectors.class'}
 for name,pin in load(offroot/'outputs-before-class-cleanup.json').items():
  if name.startswith('classes/'):assert offline['helper_classes'][name.removeprefix('classes/')]==pin
  else:assert s.sha(offroot/'vectors'/name)==pin
 assert set(offline['class_origins'])=={'official','modified'}
 for ref,pin in offline['class_origins'].items():
  for role in ('produce','verify'):assert s.sha(offroot/'vectors'/(ref+'-classes-'+role+'.log'))==pin[role+'_class_log_sha256']
 router=load(a.router_result);assert router['status']=='PASS_ENC4_WARMED_LOCAL_LOOKUP_AND_REAL_ROUTER_TRACKER' and load(a.router_result.parent/'inputs.json')['source_commit']==source
 # Root records the actual router schema/command proofs separately; this binds that reviewed receipt.
 router_binding=load(H/'router-acceptance.json');assert router_binding['status']=='PASS' and router_binding['source_commit']==source and router_binding['result_sha256']==s.sha(a.router_result)
 router_inputs=a.router_result.parent/'inputs.json'
 assert pathlib.Path(router_binding['result_path']).resolve()==a.router_result.resolve()
 assert pathlib.Path(router_binding['inputs_path']).resolve()==router_inputs.resolve() and router_binding['inputs_sha256']==s.sha(router_inputs)
 assert router_binding['gcc_result']=={'path':str((H/'runs'/('gcc-'+a.attempt)/'result.json').resolve()),'sha256':s.sha(H/'runs'/('gcc-'+a.attempt)/'result.json')}
 assert router_binding['source_adoption']=={'path':str(adoption_path),'sha256':s.sha(adoption_path)}
 assert router_binding['supervisor_sha256']==s.sha(H/'router.py')
 for flag in ('source_unchanged','provider_unchanged','generated_private_directories_removed','probe_and_classes_removed_after_hashing'):assert router_binding[flag] is True
 for flag in ('provider_rebuilt','native_core_rebuilt','old_java_or_actor_pass_pooled','discovery_or_remote_tunnels_qualified'):assert router_binding[flag] is False
 assert router_binding['both_clients_actual_accepted_ls2_key_type']==4 and router_binding['lease_key_bytes']==32 and router_binding['actual_imported_archives']==9
 public=router_binding['public_artifacts'];supervisor_roots={pathlib.Path(name).parent for name in public if pathlib.Path(name).name=='commands.json' and pathlib.Path(name).is_relative_to(H/'router-runs')};assert len(supervisor_roots)==1
 supervisor_out=supervisor_roots.pop();assert load(supervisor_out/'result.json')==router_binding
 assert router_binding['commands_sha256']==s.sha(supervisor_out/'commands.json')
 assert public[str(a.router_result.resolve())]==s.sha(a.router_result) and public[str(router_inputs.resolve())]==s.sha(router_inputs)
 for commands_path in (supervisor_out/'commands.json',a.router_result.parent/'commands.json'):
  assert public[str(commands_path.resolve())]==s.sha(commands_path)
 command_receipts(supervisor_out,router_binding,['java-version','javac-version','javac','configure','build','router'])
 for row in load(supervisor_out/'commands.json'):assert public[str(pathlib.Path(row['log']).resolve())]==row['log_sha256']
 assert [row['label'] for row in load(a.router_result.parent/'commands.json')]==['router','tracker','native']
 assert all(row['exit_code']==0 for row in load(a.router_result.parent/'commands.json'))
 for label in ('router','tracker','native','namespace'):
  log=a.router_result.parent/(label+'.log');assert public[str(log.resolve())]==s.sha(log)
 candidate=pathlib.Path(private['candidate']);assert not candidate.is_symlink() and candidate.resolve().parent==privatepath.parent.resolve()
 assert s.sha(N/'dist/BUILD_MANIFEST.json')==private['old_dist_unchanged']==OLD
 new=v.bundle(candidate);old=v.bundle(N/'dist');oldlock=(R/'migration/upstreams.lock.json').read_bytes();assert (N/'migration/upstreams.lock.json').read_bytes()==oldlock
 lock=json.loads(oldlock);before=json.loads(oldlock);cs={c['id']:c for c in lock['components']}
 def add(items,values):
  for value in values:
   if value not in items:items.append(value)
 assert (len(lock['upstreams']),len(lock['components']),len(lock['external_dependencies']))==(20,108,22)
 groups={'transport.azureus_volatile_database':[n for n in adoption['source_paths'] if 'azureus' in n or n.endswith('adapters/CMakeLists.txt') or n.endswith('tests/CMakeLists.txt')], 'services.i2cp_initial_session':[n for n in adoption['source_paths'] if '/i2cp' in n or n.endswith('tests/CMakeLists.txt')]}
 for cid,paths in groups.items():
  c=cs[cid];assert c['state']=='in_progress';add(c['native_commits'],[{'repository':'https://github.com/d00rsfan/BiglyBTC.git','commit':source,'purpose':'integration'}]);add(c['target_paths'],paths);add(c['review_records'],[(E/'REVIEW.md').as_posix()]);add(c['evidence'],[(E/'verification.json').as_posix()])
 rp=load(R/'build/azureus-maintenance-contacts-20261029/implementation/original-source-pins.json');sc=next(x for x in cs['transport.azureus_volatile_database']['source_coverage'] if x['upstream_id']=='BiglyBT');add(sc['source_paths'],list(rp['sources']));add(sc['source_symbols'],['DHTRouterImpl','DHTRouterNodeImpl','DHTRouterContactImpl','DHTControlStatsImpl','DHTPluginStorageManager'])
 sp=load(R/'build/i2cp-enc4-20261029/source-pins.json');sc=next(x for x in cs['services.i2cp_initial_session']['source_coverage'] if x['upstream_id']=='i2p-java');add(sc['source_paths'],[x['upstream_path'] for x in sp['reference_sources']['sources']]);add(sc['source_symbols'],['LeaseSet2','CreateLeaseSet2Message','KeyGenerator.generatePKIKeys','EncType.ECIES_X25519'])
 sc=next(x for x in cs['services.i2cp_initial_session']['source_coverage'] if x['upstream_id']=='azneti2phelper');add(sc['source_paths'],[x['path'] for x in sp['plugin']['files']]);add(sc['source_symbols'],['I2PHelperRouter','I2PHelperRouterDHT','I2PSMHolder'])
 for b,c in zip(before['components'],lock['components'],strict=True):
  if b['id'] not in groups:assert b==c
  else:
   assert {k:v for k,v in b.items() if k not in ('native_commits','target_paths','review_records','evidence','source_coverage')}=={k:v for k,v in c.items() if k not in ('native_commits','target_paths','review_records','evidence','source_coverage')}
   for x,y in zip(b['source_coverage'],c['source_coverage'],strict=True):
    for k in ('upstream_id','baseline_commit','first_ported_commit','integrated_through_commit','last_reviewed_commit'):assert x[k]==y[k]
 assert before['upstreams']==lock['upstreams'] and before['external_dependencies']==lock['external_dependencies']
 out=R/E;out.mkdir(parents=True);artifacts={}
 def copy(src,name):
  src=pathlib.Path(src);assert src.is_file() and not src.is_symlink() and src.stat().st_size<10*1024*1024
  with src.open('rb') as f:assert f.read(4)!=b'\x7fELF'
  dst=out/name;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,dst);artifacts[str(name)]=s.sha(dst)
 # Copy bounded public result files only. Generated profile trees and keys stay local.
 for root in [*(H/'runs').glob('*'),H/'enc4-supplement-r1',H/'maintenance-supplement-r3',H/'maintenance-supplement-r4',privatepath.parent,offroot]:
  if not root.is_dir():continue
  label='runs/'+root.name if root.parent==H/'runs' else ('offline/'+root.name if root==offroot else root.name)
  for f in root.iterdir():
   if f.is_file() and f.suffix in ('.json','.xml','.log','.txt','.jsonl'):copy(f,label+'/'+f.name)
 # Only explicit synthetic object vectors/logs; no generated Java base/config trees.
 for leaf in ('java-version.log','javac-version.log','javac.log'):
  copy(offroot/'vectors'/leaf,'offline/'+offroot.name+'/vectors/'+leaf)
 for ref in ('official','modified'):
  for leaf in (ref+'-produce.log',ref+'-native.log',ref+'-verify.log',ref+'-classes-produce.log',ref+'-classes-verify.log'):
   copy(offroot/'vectors'/leaf,'offline/'+offroot.name+'/vectors/'+leaf)
  names=['creation.clock','native.bad-signature.create',*[family+'.sender' for family in ('dsa','ed25519')]]
  names += [label+suffix for label in ('dsa-one','dsa-sixteen','ed25519-one','ed25519-sixteen') for suffix in ('.java.request','.java.create','.native.create')]
  for leaf in names:copy(offroot/'vectors'/ref/leaf,'offline/'+offroot.name+'/vectors/'+ref+'/'+leaf)
 for f in (offroot/'tools').iterdir():copy(f,'offline/'+offroot.name+'/tools/'+f.name)
 copy(supervisor_out/'result.json','router/root/result.json')
 for name,pin in router_binding['public_artifacts'].items():
  f=pathlib.Path(name);assert s.sha(f)==pin
  # Stable labels preserve individual public leaves without walking private profiles.
  copy(f,'router/'+str(f.relative_to(R/'build/i2cp-enc4-20261029/isolated-router')) if f.is_relative_to(R/'build/i2cp-enc4-20261029/isolated-router') else 'router/root/'+f.name)

 for f in H.glob('*'):
  if f.is_file() and f.suffix in ('.py','.json','.mk'):copy(f,'recipes/'+f.name)
 copy(a.private_recipe,'recipes/private.py');copy(__file__,'recipes/finish.py')
 copy(H/'router.py','recipes/router.py')
 # Native source is retained by immutable commits; source manifests, maps and reviews retain provenance.
 refs=adoption.get('source_capsules',[])+adoption.get('source_history',[])+adoption.get('correction_capsules',[])+adoption.get('source_reviews',[])+adoption.get('correction_reviews',[])
 if adoption.get('previous_adoption'):refs.append(adoption['previous_adoption'])
 seen=set();i=0
 while refs:
  ref=refs.pop(0);assert s.sha(ref['path'])==ref['sha256'];cap=pathlib.Path(ref['path'])
  if cap in seen:continue
  seen.add(cap);index=i;i+=1;copy(cap,'source/'+str(index)+'/'+cap.name);d=load(cap)
  for key in ('parent','parent_manifest'):
   if isinstance(d.get(key),dict) and 'path' in d[key]:refs.append(d[key])
  cap_artifacts=d.get('artifacts',d.get('artifacts_sha256',{}))
  for name,pin in cap_artifacts.items():
   assert s.sha(cap.parent/name)==(pin['sha256'] if isinstance(pin,dict) else pin)
   if isinstance(pin,dict):assert (cap.parent/name).stat().st_size==pin['bytes']
   if not name.startswith(('staging/','before/','original-source/','original-plugin/')) and pathlib.Path(name).suffix!='.class':copy(cap.parent/name,'source/'+str(index)+'/'+name)
 failure_history={}
 for key,values in adoption.items():
  if re.fullmatch(r'failed_.*_sha256',key):
   assert isinstance(values,dict);failure_history[key]=values
   for name,pin in values.items():
    path=pathlib.Path(name);path=path if path.is_absolute() else H/path
    assert path.resolve().is_relative_to(H) and s.sha(path)==pin
 retention_path=R/'build/i2cp-enc4-20261029/root-source-object-retention.json'
 assert s.sha(retention_path)==RETENTION_SHA;retention=load(retention_path)
 assert retention['status']=='PASS' and retention['source_commit']==sp['reference_sources']['commit'] and retention['source_jar_sha256']==sp['reference_sources']['source_jar_sha256']
 expected={row['upstream_path']:row for row in sp['reference_sources']['sources']};assert len(retention['objects'])==len(expected)==14
 for row in retention['objects']:
  original=expected.pop(row['path']);assert row['retained'] is True and row['git_blob']==original['git_blob'] and row['sha256']==original['sha256']
 assert not expected
 copy(retention_path,'source/root-source-object-retention.json')
 copy(H/'finish.py','recipes/finish-frozen-parent.py')
 copy(H/'REVIEW.md','REVIEW.md');copy(N/'dist/BUILD_MANIFEST.json','previous-dist-manifest.json')
 verification={'status':'PASS_CONTACTS_ENC4','source_commit':source,'source_paths':len(adoption['source_paths']),'lanes':lanes,'private_suites':len(private['actual_tests']),'offline_result_sha256':s.sha(offlinepath),'offline_cases_per_reference_per_direction':4,'offline_java_to_native_frames_per_reference':8,'offline_native_to_java_frames_per_reference':4,'offline_signature_negative_per_reference':1,'router_result_sha256':s.sha(a.router_result),'provider_rebuilt':False,'historical_passes_pooled':False,'whole_feature_or_performance_parity_claimed':False,'artifacts_sha256':artifacts}
 verification.update(failure_history_sha256_maps=failure_history,source_object_retention_sha256=RETENTION_SHA)
 s.write(out/'verification.json',verification);shutil.copytree(out,N/E)
 proposed=H/'proposed-upstreams.lock.json';assert not proposed.exists();s.write(proposed,lock);validations={}
 code='import json,pathlib,runpy,sys;runpy.run_path(sys.argv[1])["validate"](json.loads(pathlib.Path(sys.argv[2]).read_bytes()))'
 for root in (R,N):
  log=H/('registry-validation-'+root.name+'.log');cmd=['python3','-B','-c',code,str(root/'migration/tools/validate.py'),str(proposed)]
  with log.open('wb') as f:r=subprocess.run(cmd,cwd=root,env=dict(os.environ,GIT_NO_LAZY_FETCH='1',GIT_OPTIONAL_LOCKS='0'),stdout=f,stderr=subprocess.STDOUT)
  validations[root.name]={'command':cmd,'exit':r.returncode,'log_sha256':s.sha(log)};assert r.returncode==0,log
  copy(log,log.name);shutil.copyfile(log,N/E/log.name)
 for maps in pathlib.Path('/proc').glob('[0-9]*/maps'):
  try:t=maps.read_text()
  except OSError:continue
  assert str(N/'dist')+'/' not in t and str(privatepath.parent)+'/' not in t,'A private image remains running'
 assert s.source_snapshot(source)==snapshot and v.bundle(candidate)==new and v.bundle(N/'dist')==old and batch.provider_pins()==load(H/'runs'/('gcc-'+a.attempt)/'result.json')['provider']
 for lane in images.values():
  for img in lane.values():assert s.sha(img['path'])==img['sha256']
 assert all((root/'migration/upstreams.lock.json').read_bytes()==oldlock for root in (R,N))
 libc=ctypes.CDLL(None,use_errno=True);x=libc.renameat2;x.argtypes=[ctypes.c_int,ctypes.c_char_p,ctypes.c_int,ctypes.c_char_p,ctypes.c_uint]
 assert x(-100,os.fsencode(N/'dist'),-100,os.fsencode(candidate),2)==0,os.strerror(ctypes.get_errno())
 assert v.bundle(N/'dist')==new and v.bundle(candidate)==old;shutil.rmtree(candidate)
 for target,img in private['copied_images'].items():
  f=privatepath.parent/target;assert s.sha(f)==img['sha256'];f.unlink()
 shutil.rmtree(privatepath.parent/'test-support')
 promotion={'status':'PROMOTED_ONE_CHECKED_DEVELOPMENT_DIST','source_commit':source,'manifest_sha256':s.sha(N/'dist/BUILD_MANIFEST.json'),'leaf_count':len(new['files_sha256']),'previous_manifest_sha256':OLD,'previous_dist_removed':True,'atomic_exchange':True,'copied_test_images_removed':len(private['copied_images']),'test_support_removed':True,'both_registry_preflights_passed':True}
 s.write(H/'promotion.json',promotion);verification.update(promotion=promotion,registry_validation=validations);s.write(out/'verification.json',verification)
 for name,src in [('verification.json',out/'verification.json'),('promotion.json',H/'promotion.json'),('dist-manifest.json',N/'dist/BUILD_MANIFEST.json'),('proposed-upstreams.lock.json',proposed)]:
  if src!=out/name:shutil.copyfile(src,out/name)
  shutil.copyfile(out/name,N/E/name)
 for root in (R,N):
  target=root/'migration/upstreams.lock.json';assert target.read_bytes()==oldlock;pending=target.with_suffix('.contacts-enc4-pending');assert not pending.exists()
  with pending.open('x') as f:f.write(proposed.read_text());f.flush();os.fsync(f.fileno())
  os.replace(pending,target)
 print('PROMOTED_AND_RECORDED',source,promotion['manifest_sha256'])
if __name__=='__main__':main()
