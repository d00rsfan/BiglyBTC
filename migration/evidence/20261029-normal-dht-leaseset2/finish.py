#!/usr/bin/env python3
"""One-time accepted cohort promotion and compact evidence/registry update; no reruns."""
import ctypes,datetime,importlib.util,json,os,pathlib,shutil,subprocess
R=pathlib.Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';H=pathlib.Path(__file__).resolve().parent;E=pathlib.Path('migration/evidence/20261029-normal-dht-leaseset2')
spec=importlib.util.spec_from_file_location('private',H/'private.py');v=importlib.util.module_from_spec(spec);spec.loader.exec_module(v);s=v.s
SOURCE='6726a4cf7b017ac31d6aeede15f339c3ec2b71f7';OLD_DIST='23dac4618b19d111be7f3b877d27648a7fa95604c66f06a67eee4e3a5b0cb5cf'
def copy_file(source,target):target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,target)
def main():
 assert not (H/'promotion.json').exists() and not (R/E).exists() and not (N/E).exists()
 adoption=s.load(H/'root-adoption.json');assert adoption['source_commit']==SOURCE and adoption['source_path_count']==19
 snapshot=s.source_snapshot(SOURCE);lanes={};laneimages={}
 for lane in ('gcc','clang','san'):
  root=H/'runs'/(lane+'-r1');d=s.load(root/'result.json');assert d['status']=='PASS_NORMAL_RECEIVER_LEASESET_COHORT' and d['source_commit']==SOURCE and d['lane']==lane and d['actual_suites']==7 and d['source_unchanged'] and d['images_unchanged']
  assert s.load(root/'source-before.json')==snapshot and s.sha(root/'tests.xml')==d['tests_xml_sha256'] and s.sha(root/'ctest-full.log')==d['full_ctest_log_sha256'];s.passed_xml(root/'tests.xml',v.batch.SUITES)
  for row in s.load(root/'commands.json'):assert row.get('exit')==0 and 'exception' not in row and s.sha(row['log'])==row['log_sha256']
  assert s.sha(root/'commands.json')==d['commands_sha256'];laneimages[lane]=s.load(root/'images-before.json')
  for image in laneimages[lane].values():assert s.sha(image['path'])==image['sha256']
  lanes[lane]={'result_sha256':s.sha(root/'result.json'),'actual_suites':7,'suite_names':d['suite_names'],'compiled_products':len(laneimages[lane]),'sanitizer_environment':d['sanitizer_environment']}
 privatepath=H/'private-runs/r1/result.json';private=s.load(privatepath);assert private['status']=='PASS_PRIVATE_NORMAL_RECEIVER_LEASESET' and private['source_commit']==SOURCE and len(private['actual_tests'])==7 and private['source_unchanged']
 for lane,row in private['lane_inputs'].items():assert row['sha256']==s.sha(H/'runs'/(lane+'-r1/result.json'))
 offlinepath=H/'offline-runs/r1/result.json';offline=s.load(offlinepath);assert offline['status']=='PASS_ACTUAL_LEASESET2_OBJECTS' and offline['source_commit']==SOURCE and offline['source_unchanged'] and offline['native_images_unchanged']
 assert offline['cases_per_reference_per_direction']==4 and offline['java_messages_per_reference']==8 and offline['native_messages_per_reference']==4
 for path,d in [(privatepath,private),(offlinepath,offline)]:
  assert s.sha(path.parent/'commands.json')==d['commands_sha256']
  for row in s.load(path.parent/'commands.json'):assert row.get('exit')==0 and 'exception' not in row and s.sha(row['log'])==row['log_sha256']
 candidate=pathlib.Path(private['candidate']);assert candidate.is_relative_to(privatepath.parent) and s.sha(candidate/'BUILD_MANIFEST.json')==private['candidate_manifest_sha256'];new=v.bundle(candidate);old=v.bundle(N/'dist');assert s.sha(N/'dist/BUILD_MANIFEST.json')==private['old_dist_unchanged']==OLD_DIST
 oldmanifest=(N/'dist/BUILD_MANIFEST.json').read_bytes();lockpath=R/'migration/upstreams.lock.json';oldlock=lockpath.read_bytes();assert (N/'migration/upstreams.lock.json').read_bytes()==oldlock;lock=json.loads(oldlock);lockbefore=json.loads(oldlock)
 # Existing partial components remain partial; preserve every integration baseline.
 changed={'services.i2cp_initial_session','transport.azureus_volatile_database'}
 for c in lock['components']:
  if c['id'] not in changed:continue
  c['native_commits'].append({'repository':'https://github.com/d00rsfan/BiglyBTC.git','commit':SOURCE,'purpose':'integration'})
  c['review_records'].append((E/'REVIEW.md').as_posix());c['evidence'].append((E/'verification.json').as_posix())
  paths=[name for name in adoption['source_paths'] if ('i2cp_' in name or 'i2cp/' in name)] if c['id']=='services.i2cp_initial_session' else [name for name in adoption['source_paths'] if 'azureus' in name]
  for name in paths:
   if name not in c['target_paths']:c['target_paths'].append(name)
  for cov in c['source_coverage']:
   additions=['core/java/src/net/i2p/data/LeaseSet2.java','core/java/src/net/i2p/data/Lease2.java','core/java/src/net/i2p/data/i2cp/RequestVariableLeaseSetMessage.java','core/java/src/net/i2p/data/i2cp/CreateLeaseSet2Message.java','core/java/src/net/i2p/client/impl/RequestLeaseSetMessageHandler.java','core/java/src/net/i2p/client/impl/RequestVariableLeaseSetMessageHandler.java','core/java/src/net/i2p/crypto/KeyGenerator.java','core/java/src/net/i2p/crypto/CryptoConstants.java'] if c['id']=='services.i2cp_initial_session' and cov['upstream_id']=='i2p-java' else ['core/src/com/biglybt/core/dht/control/impl/DHTControlImpl.java','core/src/com/biglybt/core/dht/router/impl/DHTRouterImpl.java'] if c['id']=='transport.azureus_volatile_database' and cov['upstream_id']=='BiglyBT' else []
   for name in additions:
    if name not in cov['source_paths']:cov['source_paths'].append(name)
 for before,after in zip(lockbefore['components'],lock['components'],strict=True):
  assert before['state']==after['state']
  for b,a in zip(before['source_coverage'],after['source_coverage'],strict=True):
   for key in ('upstream_id','baseline_commit','first_ported_commit','integrated_through_commit','last_reviewed_commit'):assert b[key]==a[key]
 assert lockbefore['upstreams']==lock['upstreams'] and lockbefore['external_dependencies']==lock['external_dependencies']
 # A candidate is only promoted after all independent measured results passed.
 for proc in pathlib.Path('/proc').glob('[0-9]*/maps'):
  try:maps=proc.read_text()
  except (OSError,PermissionError):continue
  assert str(N/'dist')+'/' not in maps and str(privatepath.parent)+'/' not in maps,'A preview/test image is still running'
 assert s.source_snapshot(SOURCE)==snapshot
 libc=ctypes.CDLL(None,use_errno=True);exchange=libc.renameat2;exchange.argtypes=[ctypes.c_int,ctypes.c_char_p,ctypes.c_int,ctypes.c_char_p,ctypes.c_uint]
 assert exchange(-100,os.fsencode(N/'dist'),-100,os.fsencode(candidate),2)==0,os.strerror(ctypes.get_errno())
 assert v.bundle(N/'dist')==new and v.bundle(candidate)==old;shutil.rmtree(candidate)
 for target,image in private['copied_images'].items():
  copied=privatepath.parent/target;assert s.sha(copied)==image['sha256'];copied.unlink()
 shutil.rmtree(privatepath.parent/'test-support')
 promotion={'status':'PROMOTED_ONE_CHECKED_DEVELOPMENT_DIST','source_commit':SOURCE,'manifest_sha256':s.sha(N/'dist/BUILD_MANIFEST.json'),'leaf_count':len(new['files_sha256']),'previous_manifest_sha256':OLD_DIST,'previous_dist_removed':True,'atomic_exchange':True,'copied_test_images_removed':7,'test_support_removed':True};s.write(H/'promotion.json',promotion)
 out=R/E;out.mkdir(parents=True);artifacts={}
 def copy(path,name):
  path=pathlib.Path(path);assert path.is_file() and not path.is_symlink() and path.stat().st_size<10*1024*1024;dest=out/name;copy_file(path,dest);artifacts[name]=s.sha(dest)
 for name in ('run.py','private.py','offline.py','finish.py','adopt.py','root-adoption.json','approved-inputs.json','i2cp-source-review.json','recipe-source-review.json','offline-source-review.json','promotion.json'):copy(H/name,name)
 for lane in lanes:
  for leaf in (H/'runs'/(lane+'-r1')).iterdir():
   if leaf.is_file() and leaf.suffix in ('.json','.log','.xml'):copy(leaf,'runs/'+lane+'/'+leaf.name)
 for leaf in privatepath.parent.iterdir():
  if leaf.is_file() and leaf.suffix in ('.json','.log','.xml','.txt'):copy(leaf,'private/'+leaf.name)
 for leaf in offlinepath.parent.rglob('*'):
  if leaf.is_file() and leaf.suffix!='.class':copy(leaf,'offline/'+str(leaf.relative_to(offlinepath.parent)))
 for item in adoption['source_capsules']:
  cap=pathlib.Path(item['path']);label='i2cp' if 'i2cp-leaseset' in str(cap) else 'normal-receiver';copy(cap,'source/'+label+'/manifest.json')
  m=s.load(cap)
  for name in m.get('artifacts',m.get('artifacts_sha256',{})):
   if name.startswith(('staging/','before/')):continue
   copy(cap.parent/name,'source/'+label+'/'+name)
  if label=='normal-receiver':copy(R/'build/azureus-normal-store-20261029/manifest.json','source/normal-receiver/initial-parent.json')
 for item in adoption['source_reviews']:copy(item['path'],'source/reviews/'+pathlib.Path(item['path']).parent.name+'-'+pathlib.Path(item['path']).name)
 copy(N/'dist/BUILD_MANIFEST.json','dist-manifest.json');(out/'previous-dist-manifest.json').write_bytes(oldmanifest)
 verification={'status':'PASS_BOUNDED_NORMAL_DHT_RECEIVING_AND_LEASESET2','source_commit':SOURCE,'source_paths':19,'lanes':lanes,'private_commands':7,'actual_lease_cases_per_reference_per_direction':4,'actual_java_messages_per_reference':8,'actual_native_messages_per_reference':4,'provider_rebuilt':False,'provider_sha256':private['provider_sha256'],'promotion':promotion,'artifacts_sha256':artifacts,'real_profile_used':False,'public_network_activated':False,'whole_feature_or_performance_parity_claimed':False};s.write(out/'verification.json',verification)
 (out/'REVIEW.md').write_text(f"""# Normal DHT receiving and owned LeaseSet2 submission

Qualified source `{SOURCE}` changes19 exactpaths: normalreceiver11, LS2six and composedCMake2. Seven affected suites freshly passed on GCC, Clang and leak-enabled ASan/UBSan. GCC built the three full application products; Clang/SAN built seven affected test products only, with no fresh whole-application qualification claimed for those lanes. The staged private runtime ran all seven exact suite commands. Both pinned Java artifacts produced four Request37/Create41 cases each; native verified their signatures, times and actual independent pairs, then generated real new pairs and four Create41 cases verified by Java, including changed-signature refusal. No old signed-session/identity/datagram/field/version/HTTP/provider replay or historic PASS pooling.

Normal owned DHT service explicitly opts into receiving values under its same held profile/version owner. The actual bound DB/router, actual bucket traversal/local membership, exact admitted endpoint/source/token and DBexistence determine STORE admission. Two ordinary owned services on distinct generated profiles exchange direct local values both ways; the generated fixture is no longer their receiver. Fixed unseeded state still refuses forwarded caches, as original source requires; whole-bucket traversal includes unknown non-failing contacts and excludes replacements. Endpoint equality is an intentionally narrower owned subset. Payload values remain volatile; version.dat stays durable. No public/bootstrap/Host/default/republish/routing maintenance claim.

Explicit createLeaseSession consumes the owned signing identity. Default Enc0 independent ElGamal2048 pairs use OpenSSL secure RNG and constant-time public derivation in the exact group, with an explicitly documented valid exponent subset distinct from original random distribution. Fixed private/frame storage is cleansed; Qt/OS internal buffer erasure is not claimed. Genuine current Request37 invokes signed standard online LS2/Create41; renewals reuse the independent pair and last acknowledged publication policy. Existing borrowed registration behavior remains. Submitted means signed bytes queued on this socket, with no router ack, OPEN/readiness or delivery claim. Auth/TLS/other keys/LSvariants/streaming/public backend remain open.

The sole pre-build amendment corrected one pure router test's positive closest-count3to4 from exact tree geometry; no production-source change. All existing method spans retained. Two existing partial registry records gain actual commit/evidence; no states or upstream integration baselines change. One checked dist atomically replaces/deletes its predecessor, seven test copies/QtTest removed. No rollback build archive, real profile/public activation or full migration/performance completion.
""")
 shutil.copytree(out,N/E)
 for root in (R,N):
  target=root/'migration/upstreams.lock.json';assert target.read_bytes()==oldlock;pending=target.with_name(target.name+'.signed-publish-pending');assert not pending.exists()
  with pending.open('x') as stream:stream.write(json.dumps(lock,indent=2)+'\n');stream.flush();os.fsync(stream.fileno())
  os.replace(pending,target)
  log=H/('registry-validation-'+root.name+'.log')
  with log.open('wb') as stream:done=subprocess.run(['python3','-B',root/'migration/tools/validate.py'],cwd=root,env=dict(os.environ,GIT_NO_LAZY_FETCH='1',GIT_OPTIONAL_LOCKS='0'),stdout=stream,stderr=subprocess.STDOUT)
  assert done.returncode==0,log;copy_file(log,out/log.name);copy_file(log,N/E/log.name)
 print('PROMOTED_AND_RECORDED',SOURCE,promotion['manifest_sha256'],'fresh7/7/7 GCC10/Clang7/SAN7 products, private7 bothJavaLS2x4; olddistdeleted')
if __name__=='__main__':main()
