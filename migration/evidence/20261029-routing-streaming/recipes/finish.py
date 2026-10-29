#!/usr/bin/env python3
"""Root-only closeout; consume actual current gates, never rebuild or replay."""
import argparse,ctypes,importlib.util,json,os,pathlib,re,shutil,subprocess
R=pathlib.Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC'
H=R/'build/routing-streaming-followup-20261029';E=pathlib.Path('migration/evidence/20261029-routing-streaming')
OLD='0cb123398f3a5462ef6ef1e444f245dd8ce09c757bdc804213fa2f82a50ec976'
def load(p):return json.loads(pathlib.Path(p).read_text())
def module(p):
 spec=importlib.util.spec_from_file_location('qualified_private',p);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m

def main():
 p=argparse.ArgumentParser();p.add_argument('--attempt',required=True);p.add_argument('--private-recipe',required=True,type=pathlib.Path);p.add_argument('--router-result',required=True,type=pathlib.Path);a=p.parse_args()
 assert re.fullmatch(r'r[0-9]+',a.attempt)
 v=module(a.private_recipe);s=v.s;batch=v.bind_producer(H/('run-'+a.attempt+'.py'))
 adoption_path=H/('root-adoption-'+a.attempt+'.json');adoption=load(adoption_path);source=adoption['source_commit'];snapshot=s.source_snapshot(source)
 assert not (H/'promotion.json').exists() and not (R/E).exists() and not (N/E).exists()
 assert len(adoption['source_paths'])==adoption['source_path_count']==22
 for name,row in adoption['source_paths'].items():assert s.sha(N/name)==s.sha(row['after_path'])==row['after_sha256']
 def command_receipts(root,result,names=None):
  assert s.sha(root/'commands.json')==result['commands_sha256'];rows=load(root/'commands.json')
  if names is not None:assert [row['name'] for row in rows]==names
  for row in rows:assert row['exit']==0 and not row.get('exception') and s.sha(row['log'])==row['log_sha256']
 lanes={};images={}
 for lane in ('gcc','clang','san'):
  root=H/'runs'/(lane+'-'+a.attempt);d=load(root/'result.json');suites=batch.suites_for_lane(lane)
  assert d['status']=='PASS_ROUTING_STREAMING_COHORT' and d['source_commit']==source and d['source_receipt_sha256']==s.sha(adoption_path)
  assert d['actual_suites']==len(suites) and d['source_unchanged'] and d['images_unchanged'] and not d['historical_passes_pooled']
  assert d['runner_sha256']==s.sha(H/('run-'+a.attempt+'.py')) and load(root/'source-before.json')==snapshot
  assert s.sha(root/'tests.xml')==d['tests_xml_sha256'] and s.sha(root/'ctest-full.log')==d['full_ctest_log_sha256']
  assert batch.check_xml(root/'tests.xml',suites)==d['suite_names']
  command_receipts(root,d,['configure','build','inventory','tests']+(['actor-tests'] if lane!='gcc' else []))
  actor=d['actor_acceptance'];assert actor['actual_suites']==(lane!='gcc')
  if lane!='gcc':
   assert actor['status']=='PASS' and s.sha(root/'actor-tests.xml')==actor['xml_sha256'] and s.sha(root/'actor-ctest-full.log')==actor['full_log_sha256'];batch.check_xml(root/'actor-tests.xml',[batch.ACTOR_SUITE])
  images[lane]=load(root/'images-before.json');assert load(root/'images-after.json')==images[lane]
  assert set(images[lane])==set(suites.values())|{batch.ACTOR}|(set(batch.PRODUCTS) if lane=='gcc' else set())
  for img in images[lane].values():assert s.sha(img['path'])==img['sha256']
  lanes[lane]={'result_sha256':s.sha(root/'result.json'),'actual_suites':len(suites),'actor_suites':actor['actual_suites'],'compiled_products':len(images[lane]),'sanitizer_environment':d['sanitizer_environment']}
 privatepath=H/'private-runs'/a.attempt/'result.json';private=load(privatepath)
 # Private recipe deliberately exposes the same contract as the previous bounded batch.
 assert private['status']=='PASS_PRIVATE_ROUTING_STREAMING' and private['source_commit']==source and private['source_unchanged'] and not private['builds_executed'] and not private['provider_rebuilt']
 assert private['runner_sha256']==s.sha(a.private_recipe) and private['candidate_manifest_sha256']==s.sha(pathlib.Path(private['candidate'])/'BUILD_MANIFEST.json')
 assert [t['suite'] for t in private['actual_tests']]==list(batch.suites_for_lane('gcc'))+[batch.ACTOR_SUITE]
 assert set(private['copied_images'])==set(batch.suites_for_lane('gcc').values())|{batch.ACTOR}
 assert private['source_adoption_sha256']==s.sha(adoption_path) and private['producer_sha256']==s.sha(H/('run-'+a.attempt+'.py'))
 assert private['default_suites']==14 and private['separate_actor_suites']==1 and not private['historical_passes_pooled']
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
  if 'native_loader_trace' in test:
   trace=test['native_loader_trace'];assert pathlib.Path(trace['path']).resolve().parent==privatepath.parent.resolve() and s.sha(trace['path'])==trace['sha256']
 router=load(a.router_result);assert router['status']=='PASS_REAL_ISOLATED_ROUTER_STREAMING' and load(a.router_result.parent/'inputs.json')['source_commit']==source
 # Root records the actual router schema/command proofs separately; this binds that reviewed receipt.
 router_binding=load(H/'router-acceptance.json');assert router_binding['status']=='PASS' and router_binding['source_commit']==source and router_binding['result_sha256']==s.sha(a.router_result)
 router_inputs=a.router_result.parent/'inputs.json'
 assert pathlib.Path(router_binding['result_path']).resolve()==a.router_result.resolve()
 assert pathlib.Path(router_binding['inputs_path']).resolve()==router_inputs.resolve() and router_binding['inputs_sha256']==s.sha(router_inputs)
 assert router_binding['gcc_result']=={'path':str((H/'runs'/('gcc-'+a.attempt)/'result.json').resolve()),'sha256':s.sha(H/'runs'/('gcc-'+a.attempt)/'result.json')}
 assert router_binding['source_adoption']=={'path':str(adoption_path),'sha256':s.sha(adoption_path)}
 assert router_binding['supervisor_sha256']==s.sha(H/'drafts/router.py')
 for flag in ('source_unchanged','provider_unchanged','generated_private_directories_removed','probe_and_classes_removed_after_hashing'):assert router_binding[flag] is True
 for flag in ('provider_rebuilt','native_core_rebuilt','historical_actor_pass_pooled','discovery_or_remote_tunnels_qualified'):assert router_binding[flag] is False
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
 groups={'transport.azureus_volatile_database':[n for n in adoption['source_paths'] if 'azureus' in n or 'owned_ddb' in n], 'services.i2cp_initial_session':[n for n in adoption['source_paths'] if '/i2cp' in n or 'i2p_streaming' in n]}
 for cid,paths in groups.items():
  c=cs[cid];add(c['native_commits'],[{'repository':'https://github.com/d00rsfan/BiglyBTC.git','commit':source,'purpose':'integration'}]);add(c['target_paths'],paths);add(c['review_records'],[(E/'REVIEW.md').as_posix()]);add(c['evidence'],[(E/'verification.json').as_posix()])
 rp=load(R/'build/azureus-routing-bootstrap-20261029/source-pins.json');sc=next(x for x in cs['transport.azureus_volatile_database']['source_coverage'] if x['upstream_id']=='BiglyBT');add(sc['source_paths'],[n for n,d in rp['files'].items() if d['repository']=='original']);add(sc['source_symbols'],['DHTControlImpl.seed','DHTRouterImpl.seed'])
 sp=load(R/'build/i2p-streaming-20261029/source-pins.json');sc=next(x for x in cs['services.i2cp_initial_session']['source_coverage'] if x['upstream_id']=='i2p-java');add(sc['source_paths'],[x['path'] for x in sp['source_objects']]);add(sc['source_symbols'],['Packet','Connection','I2PSocketManagerFull'])
 for b,c in zip(before['components'],lock['components'],strict=True):
  if b['id'] not in groups:assert b==c
  else:
   assert b['state']==c['state']
   for x,y in zip(b['source_coverage'],c['source_coverage'],strict=True):
    for k in ('upstream_id','baseline_commit','first_ported_commit','integrated_through_commit','last_reviewed_commit'):assert x[k]==y[k]
 assert before['upstreams']==lock['upstreams'] and before['external_dependencies']==lock['external_dependencies']
 out=R/E;out.mkdir(parents=True);artifacts={}
 def copy(src,name):
  src=pathlib.Path(src);assert src.is_file() and not src.is_symlink() and src.stat().st_size<10*1024*1024
  with src.open('rb') as f:assert f.read(4)!=b'\x7fELF'
  dst=out/name;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,dst);artifacts[str(name)]=s.sha(dst)
 # Copy bounded public result files only. Generated profile trees and keys stay local.
 for root in [*(H/'runs').glob('*'),H/'actor-supplement-r1',H/'java-peer-r1',privatepath.parent]:
  if not root.is_dir():continue
  label='runs/'+root.name if root.parent==H/'runs' else root.name
  for f in root.iterdir():
   if f.is_file() and (f.suffix in ('.json','.xml','.log','.txt') or f.name.startswith('actor-loader.')):copy(f,label+'/'+f.name)
  # Exact public actor receipts only; no recursive profile/key/frame copying.
  actor=root/'actual-java-peer'
  if actor.is_dir():
   for row in ('http','loss-reorder','queue-backpressure','reset','cancel'):
    case=actor/row
    if case.is_dir():
     for leaf in ('java-command.json','java.stderr','peer-ready.json','peer-application.json','peer-result.json','bridge.json'):
      f=case/leaf
      if f.is_file():copy(f,label+'/actual-java-peer/'+row+'/'+leaf)
 copy(supervisor_out/'result.json','router/root/result.json')
 for name,pin in router_binding['public_artifacts'].items():
  f=pathlib.Path(name);assert s.sha(f)==pin
  # Stable labels preserve individual public leaves without walking private profiles.
  copy(f,'router/'+str(f.relative_to(R/'build/i2p-streaming-java-peer-20261029/real-router')) if f.is_relative_to(R/'build/i2p-streaming-java-peer-20261029/real-router') else 'router/root/'+f.name)

 for f in H.glob('*'):
  if f.is_file() and f.suffix in ('.py','.json','.mk'):copy(f,'recipes/'+f.name)
 copy(a.private_recipe,'recipes/private.py');copy(__file__,'recipes/finish.py')
 copy(H/'drafts/router.py','recipes/router.py')
 # Native source is retained by immutable commits; source manifests, maps and reviews retain provenance.
 for i,ref in enumerate(adoption.get('source_capsules',[])+adoption.get('source_history',[])+adoption.get('correction_capsules',[])+adoption.get('source_reviews',[])+adoption.get('correction_reviews',[])):
  assert s.sha(ref['path'])==ref['sha256'];cap=pathlib.Path(ref['path']);copy(cap,'source/'+str(i)+'/'+cap.name)
  d=load(cap)
  for name,pin in d.get('artifacts',d.get('artifacts_sha256',{})).items():
   assert s.sha(cap.parent/name)==pin
   if not name.startswith(('staging/','before/','original-source/','original-plugin/')) and pathlib.Path(name).suffix!='.class':copy(cap.parent/name,'source/'+str(i)+'/'+name)
 copy(H/'REVIEW.md','REVIEW.md');copy(N/'dist/BUILD_MANIFEST.json','previous-dist-manifest.json')
 verification={'status':'PASS_ROUTING_STREAMING','source_commit':source,'source_paths':22,'lanes':lanes,'private_default_suites':14,'private_actor_suites':1,'actual_original_manager_cases':5,'router_result_sha256':s.sha(a.router_result),'provider_rebuilt':False,'historical_passes_pooled':False,'whole_feature_or_performance_parity_claimed':False,'artifacts_sha256':artifacts}
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
 assert s.source_snapshot(source)==snapshot and v.bundle(candidate)==new and v.bundle(N/'dist')==old
 for lane in images.values():
  for img in lane.values():assert s.sha(img['path'])==img['sha256']
 assert all((root/'migration/upstreams.lock.json').read_bytes()==oldlock for root in (R,N))
 libc=ctypes.CDLL(None,use_errno=True);x=libc.renameat2;x.argtypes=[ctypes.c_int,ctypes.c_char_p,ctypes.c_int,ctypes.c_char_p,ctypes.c_uint]
 assert x(-100,os.fsencode(N/'dist'),-100,os.fsencode(candidate),2)==0,os.strerror(ctypes.get_errno())
 assert v.bundle(N/'dist')==new and v.bundle(candidate)==old;shutil.rmtree(candidate)
 for target,img in private['copied_images'].items():
  f=privatepath.parent/target;assert s.sha(f)==img['sha256'];f.unlink()
 shutil.rmtree(privatepath.parent/'test-support')
 promotion={'status':'PROMOTED_ONE_CHECKED_DEVELOPMENT_DIST','source_commit':source,'manifest_sha256':s.sha(N/'dist/BUILD_MANIFEST.json'),'leaf_count':len(new['files_sha256']),'previous_manifest_sha256':OLD,'previous_dist_removed':True,'atomic_exchange':True,'copied_test_images_removed':15,'test_support_removed':True,'both_registry_preflights_passed':True}
 s.write(H/'promotion.json',promotion);verification.update(promotion=promotion,registry_validation=validations);s.write(out/'verification.json',verification)
 for name,src in [('verification.json',out/'verification.json'),('promotion.json',H/'promotion.json'),('dist-manifest.json',N/'dist/BUILD_MANIFEST.json'),('proposed-upstreams.lock.json',proposed)]:
  if src!=out/name:shutil.copyfile(src,out/name)
  shutil.copyfile(out/name,N/E/name)
 for root in (R,N):
  target=root/'migration/upstreams.lock.json';assert target.read_bytes()==oldlock;pending=target.with_suffix('.routing-streaming-pending');assert not pending.exists()
  with pending.open('x') as f:f.write(proposed.read_text());f.flush();os.fsync(f.fileno())
  os.replace(pending,target)
 print('PROMOTED_AND_RECORDED',source,promotion['manifest_sha256'])
if __name__=='__main__':main()
