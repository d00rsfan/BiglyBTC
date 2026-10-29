#!/usr/bin/env python3
"""Root-only receipt closeout/promotion; no builds or qualification on import."""
import argparse,copy,ctypes,importlib.util,json,os,pathlib,re,shutil,subprocess
W=pathlib.Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('window_private',W/'private.py');v=importlib.util.module_from_spec(spec);spec.loader.exec_module(v);b=v.b;s=b.s;R=b.R;N=b.N
E=pathlib.Path('migration/evidence/20261029-public-bound-streaming-window')
ACTOR=b.ACCEPTANCE.parent/'source-pins.json'

def main():
 p=argparse.ArgumentParser();p.add_argument('--source-commit',required=True)
 for name in ('adoption','reference','gcc-result','clang-result','san-result','private-result'):p.add_argument('--'+name,type=pathlib.Path,required=True)
 a=p.parse_args();source=a.source_commit;snapshot=s.source_snapshot(source);adoption=s.load(a.adoption);assert adoption['source_commit']==source
 assert not (W/'promotion.json').exists() and not (R/E).exists() and not (N/E).exists();assert (W/'REVIEW.md').is_file()
 lanes={};maps={}
 for lane in ('gcc','clang','san'):
  result,images,inventory=b.verify_result(getattr(a,lane+'_result'),lane,source,a.adoption,a.reference);maps[lane]=images
  lanes[lane]={'path':str(getattr(a,lane+'_result')),'result_sha256':s.sha(getattr(a,lane+'_result')),'actual_suites':result['actual_suites'],'compiled_images':len(images),'sanitizer_environment':result['sanitizer_environment'],'upload_measurements':result['peer_evidence']['rows']}
 privatepath=a.private_result;private=s.load(privatepath);assert private['status']==v.STATUS and private['source_commit']==source and private['source_unchanged']
 assert private['runner_sha256']==s.sha(W/'private.py') and private['producer_sha256']==s.sha(W/'run.py') and private['source_adoption_sha256']==s.sha(a.adoption) and private['reference_result_sha256']==s.sha(a.reference)
 assert not private['builds_executed'] and not private['provider_rebuilt'] and not private['historical_passes_pooled']
 assert [x['suite'] for x in private['actual_tests']]==list(b.SUITES) and set(private['copied_images'])==set(b.SUITES.values())
 wrapper=private['namespace_wrapper'];assert pathlib.Path(wrapper['path']).resolve()==(privatepath.parent/'azureus_public_bound_namespace.py').resolve() and s.sha(wrapper['path'])==wrapper['sha256']==s.sha(b.PUBLIC_WRAPPER)
 assert private['peer_evidence']==b.peer_evidence(privatepath.parent) and s.sha(privatepath.parent/'subject-phase.json')==private['subject_phase_sha256']
 for lane,ref in private['lane_inputs'].items():assert pathlib.Path(ref['path']).resolve()==getattr(a,lane+'_result').resolve() and ref['sha256']==s.sha(ref['path'])
 assert set(private['lane_inputs'])=={'gcc','clang','san'}
 assert s.sha(privatepath.parent/'commands.json')==private['commands_sha256']
 for row in s.load(privatepath.parent/'commands.json'):assert row['exit']==0 and 'exception' not in row and s.sha(row['log'])==row['log_sha256']
 for target,image in private['copied_images'].items():assert image==maps['gcc'][target] and s.sha(privatepath.parent/target)==image['sha256']
 for row in private['actual_tests']:
  assert row['exit']==0 and s.sha(privatepath.parent/(row['suite']+'.log'))==row['log_sha256'];trace=row['native_loader_trace'];assert s.sha(trace['path'])==trace['sha256']
  for path,pin in row['initialized_images_sha256'].items():assert s.sha(path)==pin
  if 'xml_sha256' in row:
   xml=privatepath.parent/(row['suite']+'.xml');assert s.sha(xml)==row['xml_sha256'] and s.passed_xml(xml)==row['case_names']
 candidate=pathlib.Path(private['candidate']);assert not candidate.is_symlink() and candidate.resolve().parent==privatepath.parent.resolve()
 oldpin=private['old_dist_unchanged'];assert s.sha(N/'dist/BUILD_MANIFEST.json')==oldpin;old=v.bundle(N/'dist');new=v.bundle(candidate);assert s.sha(candidate/'BUILD_MANIFEST.json')==private['candidate_manifest_sha256']
 # Registry remains an additive proposal for the existing bounded component.
 oldlock=(R/'migration/upstreams.lock.json').read_bytes();assert (N/'migration/upstreams.lock.json').read_bytes()==oldlock;lock=json.loads(oldlock);before=copy.deepcopy(lock)
 components={c['id']:c for c in lock['components']};component=components['services.i2cp_initial_session'];azureus=components['transport.azureus_volatile_database'];assert component['state']==azureus['state']=='in_progress'
 def add(items,values):
  for item in values:
   if item not in items:items.append(item)
 cmake={'preview/native/services/CMakeLists.txt','preview/native/adapters/CMakeLists.txt','preview/native/tests/CMakeLists.txt'}
 public_paths=set(s.load(b.PUBLIC)['source_paths'])|cmake
 window_paths={name for name in adoption['source_paths'] if name not in public_paths}|(cmake&set(adoption['source_paths']))
 for current,paths in ((component,window_paths),(azureus,public_paths&set(adoption['source_paths']))):
  add(current['native_commits'],[{'repository':'https://github.com/d00rsfan/BiglyBTC.git','commit':source,'purpose':'integration'}]);add(current['target_paths'],sorted(paths));add(current['review_records'],[(E/'REVIEW.md').as_posix()]);add(current['evidence'],[(E/'verification.json').as_posix()])
 public_source=s.load(b.PUBLIC.parents[2]/'source-pins.json');ac=next(c for c in azureus['source_coverage'] if c['upstream_id']=='BiglyBT');assert ac['baseline_commit']==public_source['original_commit'];add(ac['source_paths'],public_source['original_sources']);add(ac['source_symbols'],['DHTTransportUDPImpl','DHTPluginImpl','DHTUDPPacketHandler','UDPNetworkManager','IpFilterImpl'])
 native=pathlib.Path(s.load(ACTOR)['native_actor']['path']);pins=s.load(native.parent/'original-source-pins.json');upstream=pins['official_commit'];assert upstream=='9134f808337b401e8e53c73734c81fab04280c9d'
 rows=pins['official_sources']+pins['additional_official_sources'];objects=[];repo=R/'build/dependencies/i2p-java-2.13.0';git_env=dict(os.environ,GIT_NO_LAZY_FETCH='1',GIT_OPTIONAL_LOCKS='0')
 for row in rows:
  path=row.get('upstream_path',row['path']);blob=subprocess.check_output(['git','-C',str(repo),'rev-parse',upstream+':'+path],env=git_env,text=True).strip();assert blob==row['git_blob']
  raw=subprocess.check_output(['git','-C',str(repo),'cat-file','blob',blob],env=git_env);assert __import__('hashlib').sha256(raw).hexdigest()==row['sha256'];objects.append({'path':path,'blob':blob,'sha256':row['sha256']})
 coverage=next(c for c in component['source_coverage'] if c['upstream_id']=='i2p-java');assert coverage['baseline_commit']==upstream
 add(coverage['source_paths'],[row['path'] for row in objects]);add(coverage['source_symbols'],['Connection','ConnectionOptions','ConnectionPacketHandler','PacketLocal','SimpleBandwidthEstimator'])
 for original,current in zip(before['components'],lock['components'],strict=True):
  if current['id'] not in ('services.i2cp_initial_session','transport.azureus_volatile_database'):assert original==current
  else:
   for key in original.keys()-{'native_commits','target_paths','review_records','evidence','source_coverage'}:assert original[key]==current[key]
   for original_coverage,current_coverage in zip(original['source_coverage'],current['source_coverage'],strict=True):
    for key in original_coverage.keys()-{'source_paths','source_symbols'}:assert original_coverage[key]==current_coverage[key]
 assert lock['upstreams']==before['upstreams'] and lock['external_dependencies']==before['external_dependencies'] and len(lock['components'])==len(before['components'])
 out=R/E;out.mkdir(parents=True);artifacts={}
 def retain(src,name):
  src=pathlib.Path(src);assert src.is_file() and not src.is_symlink() and src.stat().st_size<10*1024*1024
  with src.open('rb') as f:assert f.read(4)!=b'\x7fELF'
  assert src.suffix not in ('.class','.jar');target=out/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,target);artifacts[str(name)]=s.sha(target)
 # Retain every attempt's finite public log/receipt leaves, without private homes.
 for root in [*(W/'runs').glob('*'),*(W/'java-runs').glob('*'),privatepath.parent]:
  if not root.is_dir():continue
  label=str(root.relative_to(W))
  for file in root.iterdir():
   native_traces={pathlib.Path(row['native_loader_trace']['path']).resolve() for row in private['actual_tests']}
   if file.is_file() and (file.suffix in ('.json','.xml','.log','.txt') or file.resolve() in native_traces):retain(file,label+'/'+file.name)
  for directory,rows in (('actual-java-peer',('http','loss-reorder','queue-backpressure','reset','cancel')),('actual-java-peer-upload',('upload-cap2','upload-cap8'))):
   for row in rows:
    for leaf in ('java-command.json','java.stderr','peer-ready.json','peer-application.json','peer-result.json','bridge.json','upload-metrics.json'):
     file=root/directory/row/leaf
     if file.is_file():retain(file,label+'/'+directory+'/'+row+'/'+leaf)
 # Explicit Java public receipts; no recursive profiles/keys/raw wire frames.
 for result in [s.load(getattr(a,lane+'_result')) for lane in lanes]+[private]:
  for path,pin in result['peer_evidence']['public_files'].items():
   file=pathlib.Path(path);assert s.sha(file)==pin['sha256'] and file.stat().st_size==pin['bytes'];retain(file,str(file.relative_to(W)))
 refs=adoption.get('source_capsules',[])+adoption.get('source_reviews',[])+adoption.get('correction_capsules',[])+adoption.get('correction_reviews',[])+[{'path':str(b.ACCEPTANCE),'sha256':b.ACCEPTANCE_SHA},{'path':str(native),'sha256':s.sha(native)},{'path':str(b.PUBLIC),'sha256':b.PUBLIC_SHA},{'path':str(b.PUBLIC_REVIEW),'sha256':b.PUBLIC_REVIEW_SHA}];seen=set()
 while refs:
  ref=refs.pop(0);path=pathlib.Path(ref['path']);assert s.sha(path)==ref['sha256']
  if path in seen:continue
  index=len(seen);seen.add(path);retain(path,'source/'+str(index)+'/'+path.name);cap=s.load(path)
  for key in ('parent','parent_manifest','native_actor','acceptance_parent'):
   if isinstance(cap.get(key),dict) and 'path' in cap[key] and 'sha256' in cap[key]:refs.append(cap[key])
   elif isinstance(cap.get(key),str) and cap.get(key+'_sha256'):refs.append({'path':cap[key],'sha256':cap[key+'_sha256']})
  for name,pin in cap.get('artifacts_sha256',{}).items():
   assert s.sha(path.parent/name)==pin
   if not name.startswith(('staging/','before/','original-source/')) and pathlib.Path(name).suffix not in ('.class','.jar'):retain(path.parent/name,'source/'+str(index)+'/'+name)
 for path in (b.PUBLIC,b.PUBLIC_REVIEW,b.PUBLIC.parents[2]/'source-pins.json'):
  retain(path,'source/public-bound/'+path.name)
 retain(b.PUBLIC_WRAPPER,'source/public-bound/azureus_public_bound_namespace.py')
 for file in W.iterdir():
  if file.is_file() and file.suffix in ('.py','.mk','.json','.md'):retain(file,'recipes/'+file.name)
 retain(a.adoption,'source/root-adoption.json');retain(W/'REVIEW.md','REVIEW.md');s.write(out/'retained-source-objects.json',{'commit':upstream,'objects':objects});artifacts['retained-source-objects.json']=s.sha(out/'retained-source-objects.json')
 verification={'status':'PASS_PUBLIC_BOUND_STREAMING_WINDOW','source_commit':source,'source_paths':len(adoption['source_paths']),'lanes':lanes,'private_suites':len(private['actual_tests']),'private_unique_test_images':len(private['copied_images']),'private_upload_measurements':private['peer_evidence']['rows'],'reference_result_sha256':s.sha(a.reference),'private_result_sha256':s.sha(privatepath),'artifacts_sha256':artifacts,'historical_passes_pooled':False,'provider_rebuilt':False,'whole_backend_or_performance_parity_claimed':False}
 verification['common_suites']=len(b.COMMON);verification['gcc_suites']=len(b.SUITES);verification['namespace_wrapper_sha256']=wrapper['sha256'];verification['public_bound_inputs']=b.public_pins(adoption)
 s.write(out/'verification.json',verification);shutil.copytree(out,N/E);proposed=W/'proposed-upstreams.lock.json';assert not proposed.exists();s.write(proposed,lock);validations={}
 code='import json,pathlib,runpy,sys;runpy.run_path(sys.argv[1])["validate"](json.loads(pathlib.Path(sys.argv[2]).read_bytes()))'
 for root in (R,N):
  log=W/('registry-validation-'+root.name+'.log');cmd=['python3','-B','-c',code,str(root/'migration/tools/validate.py'),str(proposed)]
  with log.open('wb') as f:done=subprocess.run(cmd,cwd=root,env=git_env,stdout=f,stderr=subprocess.STDOUT)
  validations[root.name]={'command':cmd,'exit':done.returncode,'log_sha256':s.sha(log)};assert done.returncode==0,log;retain(log,log.name);shutil.copyfile(log,N/E/log.name)
 assert s.source_snapshot(source)==snapshot and v.bundle(candidate)==new and v.bundle(N/'dist')==old
 for lane in lanes:b.verify_result(getattr(a,lane+'_result'),lane,source,a.adoption,a.reference)
 assert all((root/'migration/upstreams.lock.json').read_bytes()==oldlock for root in (R,N))
 for maps in pathlib.Path('/proc').glob('[0-9]*/maps'):
  try:loaded=maps.read_text()
  except OSError:continue
  assert str(N/'dist')+'/' not in loaded and str(privatepath.parent)+'/' not in loaded,'Private images remain mapped by a running process'
 libc=ctypes.CDLL(None,use_errno=True);exchange=libc.renameat2;exchange.argtypes=[ctypes.c_int,ctypes.c_char_p,ctypes.c_int,ctypes.c_char_p,ctypes.c_uint]
 assert exchange(-100,os.fsencode(N/'dist'),-100,os.fsencode(candidate),2)==0,os.strerror(ctypes.get_errno())
 assert v.bundle(N/'dist')==new and v.bundle(candidate)==old;shutil.rmtree(candidate)
 for target,image in private['copied_images'].items():file=privatepath.parent/target;assert s.sha(file)==image['sha256'];file.unlink()
 shutil.rmtree(privatepath.parent/'test-support');pathlib.Path(wrapper['path']).unlink()
 promotion={'status':'PROMOTED_ONE_CHECKED_DEVELOPMENT_DIST','source_commit':source,'manifest_sha256':s.sha(N/'dist/BUILD_MANIFEST.json'),'leaf_count':len(new['files_sha256']),'previous_manifest_sha256':oldpin,'previous_dist_removed':True,'atomic_exchange':True,'copied_test_images_removed':len(private['copied_images']),'test_support_removed':True,'both_registry_preflights_passed':True}
 s.write(W/'promotion.json',promotion);verification.update(promotion=promotion,registry_validation=validations);s.write(out/'verification.json',verification)
 for name,src in [('verification.json',out/'verification.json'),('promotion.json',W/'promotion.json'),('dist-manifest.json',N/'dist/BUILD_MANIFEST.json'),('proposed-upstreams.lock.json',proposed)]:
  if src!=out/name:shutil.copyfile(src,out/name)
  shutil.copyfile(out/name,N/E/name)
 for root in (R,N):
  target=root/'migration/upstreams.lock.json';assert target.read_bytes()==oldlock;pending=target.with_suffix('.streaming-window-pending');assert not pending.exists()
  with pending.open('x') as f:f.write(proposed.read_text());f.flush();os.fsync(f.fileno())
  os.replace(pending,target)
 print('PROMOTED_AND_RECORDED',source,promotion['manifest_sha256'])
if __name__=='__main__':main()
