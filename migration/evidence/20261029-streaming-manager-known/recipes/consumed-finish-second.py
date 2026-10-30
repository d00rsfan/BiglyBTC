#!/usr/bin/env python3
"""Root-only associated M closeout/promotion after all actual gates; no subjects/builds."""
import argparse,copy,ctypes,importlib.util,json,os,pathlib,re,shutil,subprocess
W=pathlib.Path(__file__).resolve().parent;C=W.parent
spec=importlib.util.spec_from_file_location('private_public',C/'private-public/run.py');q=importlib.util.module_from_spec(spec);spec.loader.exec_module(q);v=q.p;a1=q.a1;b=q.b;s=q.s;R=v.R;N=b.N
E=pathlib.Path('migration/evidence/20261029-streaming-manager-known')
MANAGER=R/'build/i2p-streaming-manager-20261029/implementation/amendments/hard-deadline-and-renewal/manifest.json'
KNOWN=R/'build/azureus-known-peer-attachment-20261029/amendments/final-preparation-permit/amendments/ordered-exact-peer-grant/manifest.json'
ORIGINAL=R/'build/i2p-streaming-manager-20261029/implementation/original-source-pins.json'
KNOWN_ORIGINAL=R/'build/azureus-known-peer-attachment-20261029/source-pins.json'
PUBLIC_RUN=a1.D/'recover-public-r2.py';PUBLIC_REVIEW=a1.D/'recover-public-r2-review.json'

def recipe_pins():
 cap=s.load(W/'manifest.json')
 for name,pin in cap['artifacts_sha256'].items():assert s.sha(W/name)==pin
 for name,pin in cap['inputs_sha256'].items():assert s.sha(name)==pin
 a1.pins()

def cached_public(path,source,adoption,image,current):
 assert path.name=='result.json' and path.parent.parent.resolve()==a1.D.resolve() and re.fullmatch(r'public-recovery-r[2-9][0-9]*',path.parent.name)
 root=path.parent;value=s.load(path);name=root.name.replace('public-recovery','recover-public');run=a1.D/(name+'.py');review=a1.D/(name+'-review.json');approved=s.load(review)
 original=PUBLIC_RUN.read_text();needle="out = W / 'public-recovery-r2'";assert original.count(needle)==1 and run.read_text()==original.replace(needle,"out = W / '"+root.name+"'")
 assert approved['status']=='ROOT_REVIEWED_ONE_EXACT_PUBLIC_SUBJECT_ONLY' and approved['source_commit']==source and approved['no_builds_or_javac'] and approved['old_failure_retained'] and approved['no_policy_or_namespace_scope_change']
 assert value['status']=='PASS_EXACT_CURRENT_PUBLIC_RECOVERY' and value['source_commit']==source and value['runner_sha256']==s.sha(run)==approved['runner_sha256']
 assert value['builds_executed'] is False and value['javac_invocations']==0 and value['source_unchanged'] and value['dependencies_unchanged'] and value['image']==image and value['original_head_index_unchanged']==b.protected_original()
 for name,pin in value['artifacts_sha256'].items():assert s.sha(root/name)==pin
 assert s.load(root/'source-before.json')==current and s.load(root/'images-before.json')=={b.SUITES['azureus-public-bound']:image}
 deps=s.load(root/'dependencies-before.json');assert b.base.file_pins(deps)==deps
 commands=s.load(root/'commands.json');assert s.sha(root/'commands.json')==value['commands_sha256'] and [x['name'] for x in commands]==['inventory','tests']
 for row in commands:assert row['exit']==0 and 'exception' not in row and s.sha(row['log'])==row['log_sha256']
 selected=next(row for row in s.load(root/'inventory.log')['tests'] if row['name']=='azureus-public-bound');assert selected['command']==['/usr/bin/python3',str(b.PUBLIC_WRAPPER),image['path']] and {x['name']:x['value'] for x in selected['properties']}['TIMEOUT']==1000
 rows=a1.xml_rows(root/'tests.xml');assert set(rows)=={'azureus-public-bound'} and all(a1.passed(row) for row in rows.values())
 assert value['namespace']==b.current_namespace(root,b.public_pins(adoption)['namespace_admin'],selected['command'][2:],b.PUBLIC_LAUNCHER)
 return dict(value,runner_ref=a1.ref(run),recipe_review_ref=a1.ref(review))

def main():
 p=argparse.ArgumentParser();p.add_argument('--source-commit',required=True)
 for name in ('adoption','reference','gcc-result','clang-result','san-result','private-result','public-result'):p.add_argument('--'+name,type=pathlib.Path,required=True)
 a=p.parse_args();source=a.source_commit;recipe_pins();protected=b.protected_original();snapshot=a1.source_guard(source,s.load(a.adoption));adoption=s.load(a.adoption);assert a.adoption.resolve()==a1.ADOPTION.resolve() and a.reference.resolve()==a1.REFERENCE.resolve()
 assert not (W/'promotion.json').exists() and not (R/E).exists() and not (N/E).exists() and (W/'REVIEW.md').is_file()
 lanes={};maps={}
 for lane in ('gcc','clang','san'):
  result,images,inventory=v.verify_result(getattr(a,lane+'_result'),lane,source,a.adoption,a.reference);maps[lane]=images
  lanes[lane]={'path':str(getattr(a,lane+'_result')),'result_sha256':s.sha(getattr(a,lane+'_result')),'associated_suites':result['associated_total_suites'],'fresh_actual_suites':result['fresh_actual_suites'],'retained_exact_r2_suites':result['retained_r2_passed_suites'],'fresh_full_cohort':False,'compiled_images':len(images),'sanitizer_environment':({'ASAN_OPTIONS':'detect_leaks=1:halt_on_error=1','UBSAN_OPTIONS':'halt_on_error=1:print_stacktrace=1'} if lane=='san' else {}),'sanitizer_environment_origin':a1.ref(a1.W/'run.py'),'upload_measurements':result['peer_evidence']['rows'],'actual_manager_applications':result['manager_evidence']['actual_applications']}
 public=cached_public(a.public_result,source,adoption,maps['gcc'][b.SUITES['azureus-public-bound']],snapshot)
 privatepath=a.private_result;privatepin=s.sha(privatepath);private=s.load(privatepath);assert private['status']==q.STATUS and private['source_commit']==source and private['source_unchanged'] and private['runner_sha256']==s.sha(C/'private-public/run.py')
 assert private['actual_subjects']==6 and private['fresh_subjects']==1 and private['retained_private_subjects']==5 and private['builds_executed'] is False and private['stage_executed'] is False and private['new_javac_invocations']==0 and private['promotable'] is False
 for name,pin in private['artifacts_sha256'].items():assert s.sha(privatepath.parent/name)==pin
 fivepath=pathlib.Path(private['private_five_origin']['path']);assert s.sha(fivepath)==private['private_five_origin']['sha256'];five=q.five_origin(fivepath,source);privateroot=fivepath.parent
 assert private['original_head_index_unchanged']==protected and private['java_reference']==a1.reference_pins(source)
 assert private['lane_inputs']==five['lane_inputs'] and private['actual_tests']==[*five['actual_tests'],private['fresh_public']] and private['copied_images']==five['copied_images']
 expected=[*v.SELECTORS,('public-known','azureus-public-bound',q.SELECTOR)];assert [(x['label'],x['suite'],x['selector']) for x in private['actual_tests']]==expected
 wrapper=private['namespace_wrapper'];companion=private['namespace_launcher'];assert wrapper==five['namespace_wrapper'] and companion==five['namespace_launcher']
 namespace_row=private['fresh_public'];assert private['namespace_admin']==b.current_namespace(privatepath.parent,b.public_pins(adoption)['namespace_admin'],namespace_row['arguments'][2:],pathlib.Path(companion['path']))
 assert private['namespace_admin_receipt']==b.namespace_receipt(privatepath.parent/'namespace-admin.json',private['namespace_admin'],namespace_row['arguments'][2:],pathlib.Path(companion['path']))
 assert s.sha(privatepath.parent/'commands.json')==private['commands_sha256']
 for row in s.load(privatepath.parent/'commands.json'):assert row['exit']==0 and 'exception' not in row and s.sha(row['log'])==row['log_sha256']
 for lane,row in private['lane_inputs'].items():assert pathlib.Path(row['path']).resolve()==getattr(a,lane+'_result').resolve() and row['sha256']==s.sha(row['path'])
 for target,image in private['copied_images'].items():assert image==maps['gcc'][target] and s.sha(privateroot/target)==image['sha256']
 for row in private['actual_tests']:
  root=privatepath.parent if row['label']=='public-known' else privateroot;assert row['exit']==0 and s.sha(root/(row['label']+'.log'))==row['log_sha256'];trace=row['native_loader_trace'];assert s.sha(trace['path'])==trace['sha256']
  xml=root/(row['label']+'.xml');assert s.sha(xml)==row['xml_sha256'] and s.passed_xml(xml)==row['case_names']
  for path,pin in row['initialized_images_sha256'].items():assert s.sha(path)==pin
 candidate=pathlib.Path(private['candidate']);assert candidate==pathlib.Path(five['candidate']) and not candidate.is_symlink() and candidate.resolve().parent==privateroot.resolve()
 oldpin=private['old_dist_unchanged'];assert s.sha(N/'dist/BUILD_MANIFEST.json')==oldpin;old=v.bundle(N/'dist');new=v.bundle(candidate);assert s.sha(candidate/'BUILD_MANIFEST.json')==private['candidate_manifest_sha256']
 # Registry remains an additive proposal for the existing bounded component.
 oldlock=(R/'migration/upstreams.lock.json').read_bytes();assert (N/'migration/upstreams.lock.json').read_bytes()==oldlock;lock=json.loads(oldlock);before=copy.deepcopy(lock)
 components={c['id']:c for c in lock['components']};component=components['services.i2cp_initial_session'];azureus=components['transport.azureus_volatile_database'];assert component['state']==azureus['state']=='in_progress'
 def add(items,values):
  for item in values:
   if item not in items:items.append(item)
 cmake={'preview/native/services/CMakeLists.txt','preview/native/tests/CMakeLists.txt'}
 manager_paths=set(s.load(MANAGER)['source_paths'])|set(s.load(b.ACCEPTANCE)['source_paths'])|cmake
 known_paths=set(s.load(KNOWN)['source_paths'])|{'preview/native/tests/CMakeLists.txt'}
 for current,paths in ((component,manager_paths),(azureus,known_paths)):
  add(current['native_commits'],[{'repository':'https://github.com/d00rsfan/BiglyBTC.git','commit':source,'purpose':'integration'}]);add(current['target_paths'],sorted(paths));add(current['review_records'],[(E/'REVIEW.md').as_posix()]);add(current['evidence'],[(E/'verification.json').as_posix()])
 git_env=dict(os.environ,GIT_NO_LAZY_FETCH='1',GIT_OPTIONAL_LOCKS='0');objects=[];pins=s.load(ORIGINAL);upstream=pins['official_commit'];assert upstream=='9134f808337b401e8e53c73734c81fab04280c9d'
 repo=R/'build/dependencies/i2p-java-2.13.0'
 for row in pins['official_sources']+pins['additional_official_sources']:
  path=row.get('upstream_path',row['path']);blob=subprocess.check_output(['git','-C',str(repo),'rev-parse',upstream+':'+path],env=git_env,text=True).strip();assert blob==row['git_blob']
  raw=subprocess.check_output(['git','-C',str(repo),'cat-file','blob',blob],env=git_env);assert __import__('hashlib').sha256(raw).hexdigest()==row['sha256'];objects.append({'path':path,'blob':blob,'sha256':row['sha256'],'commit':upstream})
 coverage=next(c for c in component['source_coverage'] if c['upstream_id']=='i2p-java');assert coverage['baseline_commit']==upstream
 add(coverage['source_paths'],[row['path'] for row in objects]);add(coverage['source_symbols'],['ConnectionManager','ConnectionHandler','MessageHandler','I2PSocketManager','I2PServerSocket','ConnectionOptions'])
 known=s.load(KNOWN_ORIGINAL);ac=next(c for c in azureus['source_coverage'] if c['upstream_id']=='BiglyBT');assert ac['baseline_commit']==known['original_commit']
 for path,row in known['original_sources'].items():
  blob=subprocess.check_output(['git','-C',str(R),'rev-parse',known['original_commit']+':'+path],env=git_env,text=True).strip();assert blob==row['git_blob']
  raw=subprocess.check_output(['git','-C',str(R),'cat-file','blob',blob],env=git_env);assert __import__('hashlib').sha256(raw).hexdigest()==row['sha256'];objects.append({'path':path,'blob':blob,'sha256':row['sha256'],'commit':known['original_commit']})
 add(ac['source_paths'],known['original_sources']);add(ac['source_symbols'],['DHTTransportUDPImpl','DHTTransportUDPContactImpl','DHTControlImpl','DHTRouterImpl','DHTUDPUtils'])
 for original,current in zip(before['components'],lock['components'],strict=True):
  if current['id'] not in ('services.i2cp_initial_session','transport.azureus_volatile_database'):assert original==current
  else:
   for key in original.keys()-{'native_commits','target_paths','review_records','evidence','source_coverage'}:assert original[key]==current[key]
   for original_coverage,current_coverage in zip(original['source_coverage'],current['source_coverage'],strict=True):
    for key in original_coverage.keys()-{'source_paths','source_symbols'}:assert original_coverage[key]==current_coverage[key]
 assert len(lock['upstreams'])==20 and len(lock['components'])==108 and len(lock['external_dependencies'])==22
 assert lock['upstreams']==before['upstreams'] and lock['external_dependencies']==before['external_dependencies'] and len(lock['components'])==len(before['components'])
 out=R/E;out.mkdir(parents=True);artifacts={}
 def retain(src,name):
  src=pathlib.Path(src);assert src.is_file() and not src.is_symlink() and src.stat().st_size<10*1024*1024
  with src.open('rb') as f:assert f.read(4)!=b'\x7fELF'
  assert src.suffix not in ('.class','.jar');target=out/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,target);artifacts[str(name)]=s.sha(target)
 # Retain exact failed/successful origins and fresh affected rows, never synthetic full PASS XML.
 origins=[]
 for label,origin in (('original-M-r1',b.PREVIOUS),('original-full-r2',a1.P),('affected-a1',a1.A),('affected-a2',a1.W),('private-five',v.W),('private-public',q.W)):
  for kind in ('runs','java-runs','private-runs'):
   for root in (origin/kind).glob('*'):
    if root.is_dir():origins.append((label+'/'+kind+'/'+root.name,root))
 for label,root in (('diagnostic-gcc-d1',a1.D/'two-selectors/runs/gcc-d1'),('diagnostic-san-d1',a1.D/'two-selectors/runs/san-d1'),('diagnostic-san-d2',a1.HELPER.parent/'runs/san-d2'),('failed-Public-r1',a1.D/'public-recovery-r1'),('failed-Public-r2',a1.D/'public-recovery-r2'),('actual-Public-later',a.public_result.parent),('diagnostic-java',a1.D/'java-prepared-r1'),('fixture-java',a1.REFERENCE.parent)):
  if root.is_dir():origins.append((label,root))
 nested={'java-command.json','java.stderr','peer-ready.json','peer-application.json','peer-result.json','bridge.json','upload-metrics.json','native-acceptance.json','http-9000.json','http-9001.json','inbound-application.json','cancel-application.json','packet-metadata-tail.json','owned-logger.config','owned-streaming-0.log','owned-streaming-1.log','inputs-before.json','java-version.log','javac-version.log','javac.log'}
 for label,root in origins:
  for file in root.rglob('*'):
   direct=file.parent==root
   if file.is_file() and not file.is_symlink() and ((direct and (file.suffix in ('.json','.xml','.log','.txt') or file.name.startswith('native-loader-'))) or (not direct and file.name in nested)):
    retain(file,label+'/'+str(file.relative_to(root)))
 # Explicit per-row associations and current helper/fixture originals stay separate.
 for lane in lanes:
  current=s.load(getattr(a,lane+'_result'))
  for group in (current['manager_evidence'],current['peer_evidence']):
   for path,pin in group['public_files'].items():
    file=pathlib.Path(path);assert s.sha(file)==pin['sha256'] and file.stat().st_size==pin['bytes']
 for group in (five['manager_evidence'],five['retained_legacy_http']):
  for path,pin in group['public_files'].items():assert s.sha(path)==pin['sha256'] and pathlib.Path(path).stat().st_size==pin['bytes']
 refs=adoption.get('source_capsules',[])+adoption.get('source_reviews',[])+[{'path':str(a.adoption),'sha256':s.sha(a.adoption)},{'path':str(a.reference),'sha256':s.sha(a.reference)},{'path':str(ORIGINAL),'sha256':s.sha(ORIGINAL)},{'path':str(KNOWN_ORIGINAL),'sha256':s.sha(KNOWN_ORIGINAL)}]+[a1.ref(path) for path in (a1.F/'manifest.json',a1.F/'root-review.json',a1.D/'fin-callback-order-proof.json',a1.W/'manifest.json',v.W/'manifest.json',q.W/'manifest.json',PUBLIC_REVIEW)]+[public['recipe_review_ref']];seen=set()
 while refs:
  ref=refs.pop(0);path=pathlib.Path(ref['path']);assert s.sha(path)==ref['sha256']
  if path in seen:continue
  index=len(seen);seen.add(path);retain(path,'source/'+str(index)+'/'+path.name);cap=s.load(path)
  for key in ('parent','parent_manifest','native_actor','native_manager','native_manager_review','acceptance_parent','previous_adoption','parent_reference','fixture_manifest','parent_effective_adoption','parent_java','original_callback_proof','diagnostic_adoption','original_m_adoption'):
   if isinstance(cap.get(key),dict) and 'path' in cap[key] and 'sha256' in cap[key]:refs.append(cap[key])
   elif isinstance(cap.get(key),str) and cap.get(key+'_sha256'):refs.append({'path':cap[key],'sha256':cap[key+'_sha256']})
  refs.extend(row for row in cap.get('input_pins',{}).values() if isinstance(row,dict) and 'path' in row and 'sha256' in row)
  for key in ('source_capsules','source_reviews','correction_capsules','correction_reviews','source_history'):refs.extend(cap.get(key,[]))
  for key,values in cap.items():
   if re.fullmatch(r'failed_.*_sha256',key):
    assert isinstance(values,dict)
    for name,pin in values.items():
     failed=pathlib.Path(name);failed=failed if failed.is_absolute() else W/failed
     assert (failed.resolve().is_relative_to(W) or failed.resolve().is_relative_to(b.PREVIOUS)) and s.sha(failed)==pin
  for name,pin in cap.get('artifacts_sha256',{}).items():
   assert s.sha(path.parent/name)==pin
   if not name.startswith(('staging/','before/','original-source/')) and pathlib.Path(name).suffix not in ('.class','.jar'):retain(path.parent/name,'source/'+str(index)+'/'+name)
 for label,row in adoption['w_inputs'].items():retain(row['path'],'prior-W/'+label+'.json')
 retain(b.PUBLIC_WRAPPER,'source/azureus_public_bound_namespace.py');retain(b.PUBLIC_LAUNCHER,'source/azureus_public_admin_launcher.py')
 namespace=private['namespace_admin']
 for key in ('preflight','source_capsule','source_review','environment_preflight','root_approval','compiled_receipt','readonly_launch','launch_receipt'):retain(namespace[key]['path'],'prior-W/namespace-'+key+'.json')
 retain(b.ADMIN_RECIPES,'prior-W/namespace-recipes.json');retain(b.ADMIN_VALIDATOR,'prior-W/namespace-admin.py')
 outer=pathlib.Path(namespace['environment_preflight']['path'])
 for name,pin in s.load(outer)['logs_sha256'].items():assert s.sha(outer.parent/name)==pin;retain(outer.parent/name,'prior-W/readonly-'+name)

 # Frozen parent r1 remains FAIL; its retained rows receive no PASS credit.
 b.failed_origin_pins()
 for file in W.iterdir():
  if file.is_file() and file.suffix in ('.py','.mk','.json','.md'):retain(file,'recipes/'+file.name)
 for name,row in s.load(a1.F/'manifest.json')['java_sources'].items():
  assert s.sha(row['after_path'])==row['after_sha256'];retain(row['after_path'],'recipes/fixture-java/'+name)
 retain(a.adoption,'source/root-adoption.json');retain(PUBLIC_RUN,'recipes/recover-public-r2.py');retain(public['runner_ref']['path'],'recipes/'+pathlib.Path(public['runner_ref']['path']).name);retain(a1.F/'adopt-and-prepare.py','recipes/adopt-and-prepare.py');retain(W/'REVIEW.md','REVIEW.md');s.write(out/'retained-source-objects.json',{'objects':objects});artifacts['retained-source-objects.json']=s.sha(out/'retained-source-objects.json')
 verification={'status':'PASS_STREAMING_MANAGER_KNOWN_ASSOCIATED_ROWS','source_commit':source,'implementation_source_commit':'43d642fb0025510c581ebdc4d7a332752c511a46','source_paths':len(adoption['source_paths']),'qualification_mode':a1.MODE,'fresh_full_cohort':False,'lanes':lanes,'private_suites':6,'private_unique_test_images':6,'private_scope':'five prior exact private selectors plus one fresh authenticated Public; not all22 privately','private_actual_manager':five['manager_evidence']['native'],'lane_actual_manager_applications':{lane:4 for lane in lanes},'reference_result_sha256':s.sha(a.reference),'private_result_sha256':s.sha(privatepath),'private_five_origin':private['private_five_origin'],'private_six_origin':a1.ref(privatepath),'cached_gcc_public':a1.ref(a.public_result),'cached_gcc_public_namespace':public['namespace'],'cached_gcc_public_runner':public['runner_ref'],'cached_gcc_public_recipe_review':public['recipe_review_ref'],'artifacts_sha256':artifacts,'historical_different_source_passes_pooled':False,'provider_rebuilt':False,'whole_backend_or_performance_parity_claimed':False,'earlier_failures_preserved':True,'stop_after_current_milestone':True,'runtime_limits':'Source-backed Java fixture callback-order race fenced; earlier SYN/close/startup failures retained; broader intermittent SYN cause and stability/performance parity unestablished'}
 verification['failed_current_attempt']=b.failed_origin_pins();verification['helper_preparation']={'qualification_javac_invocations':0,'actual_fixture_preparation':a1.ref(a.reference),'actual_preparation_javac_invocations':1,'prepared_classes':4,'fixture':a1.ref(a1.F/'manifest.json'),'actual_callback_bytecode_proof':a1.ref(a1.D/'fin-callback-order-proof.json')}
 verification['common_suites']=19;verification['gcc_suites']=22;verification['fresh_suites_per_lane']=3;verification['retained_exact_r2_suites']={'gcc':18,'clang':16,'san':16};verification['namespace_wrapper_sha256']=wrapper['sha256'];verification['namespace_admin']=private['namespace_admin'];verification['w_inputs']=adoption['w_inputs'];verification['original_head_index_unchanged']=protected;verification['public_bound_inputs']=b.public_pins(adoption)
 s.write(out/'verification.json',verification);shutil.copytree(out,N/E);proposed=W/'proposed-upstreams.lock.json';assert not proposed.exists();s.write(proposed,lock);validations={}
 code='import json,pathlib,runpy,sys;runpy.run_path(sys.argv[1])["validate"](json.loads(pathlib.Path(sys.argv[2]).read_bytes()))'
 for root in (R,N):
  log=W/('registry-validation-'+root.name+'.log');cmd=['python3','-B','-c',code,str(root/'migration/tools/validate.py'),str(proposed)]
  with log.open('wb') as f:done=subprocess.run(cmd,cwd=root,env=git_env,stdout=f,stderr=subprocess.STDOUT)
  validations[root.name]={'command':cmd,'exit':done.returncode,'log_sha256':s.sha(log)};assert done.returncode==0,log;retain(log,log.name);shutil.copyfile(log,N/E/log.name)
 assert b.protected_original()==protected and a1.source_guard(source,adoption)==snapshot and v.bundle(candidate)==new and v.bundle(N/'dist')==old
 for lane in lanes:v.verify_result(getattr(a,lane+'_result'),lane,source,a.adoption,a.reference)
 assert s.sha(privatepath)==privatepin and q.five_origin(fivepath,source)==five and cached_public(a.public_result,source,adoption,maps['gcc'][b.SUITES['azureus-public-bound']],snapshot)==public
 assert all((root/'migration/upstreams.lock.json').read_bytes()==oldlock for root in (R,N))
 for maps in pathlib.Path('/proc').glob('[0-9]*/maps'):
  try:loaded=maps.read_text()
  except OSError:continue
  assert str(N/'dist')+'/' not in loaded and str(privateroot)+'/' not in loaded and str(privatepath.parent)+'/' not in loaded,'Private images remain mapped by a running process'
 libc=ctypes.CDLL(None,use_errno=True);exchange=libc.renameat2;exchange.argtypes=[ctypes.c_int,ctypes.c_char_p,ctypes.c_int,ctypes.c_char_p,ctypes.c_uint]
 assert exchange(-100,os.fsencode(N/'dist'),-100,os.fsencode(candidate),2)==0,os.strerror(ctypes.get_errno())
 assert v.bundle(N/'dist')==new and v.bundle(candidate)==old;shutil.rmtree(candidate)
 for target,image in private['copied_images'].items():file=privateroot/target;assert s.sha(file)==image['sha256'];file.unlink()
 shutil.rmtree(privateroot/'test-support');pathlib.Path(wrapper['path']).unlink();pathlib.Path(companion['path']).unlink()
 promotion={'status':'PROMOTED_ONE_CHECKED_DEVELOPMENT_DIST','source_commit':source,'manifest_sha256':s.sha(N/'dist/BUILD_MANIFEST.json'),'leaf_count':len(new['files_sha256']),'previous_manifest_sha256':oldpin,'previous_dist_removed':True,'atomic_exchange':True,'copied_test_images_removed':len(private['copied_images']),'test_support_removed':True,'namespace_wrapper_and_launcher_removed':True,'both_registry_preflights_passed':True}
 s.write(W/'promotion.json',promotion);verification.update(promotion=promotion,registry_validation=validations);s.write(out/'verification.json',verification)
 for name,src in [('verification.json',out/'verification.json'),('promotion.json',W/'promotion.json'),('dist-manifest.json',N/'dist/BUILD_MANIFEST.json'),('proposed-upstreams.lock.json',proposed)]:
  if src!=out/name:shutil.copyfile(src,out/name)
  shutil.copyfile(out/name,N/E/name)
 for root in (R,N):
  target=root/'migration/upstreams.lock.json';assert target.read_bytes()==oldlock;pending=target.with_suffix('.manager-known-pending');assert not pending.exists()
  with pending.open('x') as f:f.write(proposed.read_text());f.flush();os.fsync(f.fileno())
  os.replace(pending,target)
 assert b.protected_original()==protected and (R/'migration/upstreams.lock.json').read_bytes()==(N/'migration/upstreams.lock.json').read_bytes()
 print('PROMOTED_AND_RECORDED',source,promotion['manifest_sha256'])
if __name__=='__main__':main()
