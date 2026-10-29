#!/usr/bin/env python3
"""One-time accepted cohort promotion and compact evidence/registry update; no reruns."""
import ctypes,datetime,importlib.util,json,os,pathlib,shutil,subprocess
R=pathlib.Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';H=pathlib.Path(__file__).resolve().parent;E=pathlib.Path('migration/evidence/20261029-signed-session-local-publish')
spec=importlib.util.spec_from_file_location('private',H/'private.py');v=importlib.util.module_from_spec(spec);spec.loader.exec_module(v);s=v.s
SOURCE='e124abb90ab846c4b057a6b868004c0acb737118';OLD_DIST='94ee9178856a54b67655a28f825b0823fc1125d2ca7016fa59cdca5382e2fd51'
def copy_file(source,target):target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,target)
def main():
 assert not (H/'promotion.json').exists() and not (R/E).exists() and not (N/E).exists()
 adoption=s.load(H/'root-adoption.json');assert adoption['source_commit']==SOURCE and adoption['source_path_count']==21
 snapshot=s.source_snapshot(SOURCE);lanes={};laneimages={}
 for lane in ('gcc','clang','san'):
  root=H/'runs'/(lane+'-r1');d=s.load(root/'result.json');assert d['status']=='PASS_SIGNED_SESSION_LOCAL_PUBLISH_COHORT' and d['source_commit']==SOURCE and d['lane']==lane and d['actual_suites']==7 and d['source_unchanged'] and d['images_unchanged']
  assert s.load(root/'source-before.json')==snapshot and s.sha(root/'tests.xml')==d['tests_xml_sha256'] and s.sha(root/'ctest-full.log')==d['full_ctest_log_sha256'];s.passed_xml(root/'tests.xml',v.batch.SUITES)
  for row in s.load(root/'commands.json'):assert row.get('exit')==0 and 'exception' not in row and s.sha(row['log'])==row['log_sha256']
  assert s.sha(root/'commands.json')==d['commands_sha256'];laneimages[lane]=s.load(root/'images-before.json')
  for image in laneimages[lane].values():assert s.sha(image['path'])==image['sha256']
  lanes[lane]={'result_sha256':s.sha(root/'result.json'),'actual_suites':7,'suite_names':d['suite_names'],'compiled_products':len(laneimages[lane]),'sanitizer_environment':d['sanitizer_environment']}
 privatepath=H/'private-runs/r1/result.json';private=s.load(privatepath);assert private['status']=='PASS_PRIVATE_SIGNED_SESSION_LOCAL_PUBLISH' and private['source_commit']==SOURCE and len(private['actual_tests'])==7 and private['source_unchanged']
 for lane,row in private['lane_inputs'].items():assert row['sha256']==s.sha(H/'runs'/(lane+'-r1/result.json'))
 offlinepath=H/'offline-runs/r3/result.json';offline=s.load(offlinepath);assert offline['status']=='PASS_ACTUAL_SESSION_OBJECTS_AND_LOCAL_VALUE_FIELDS' and offline['source_commit']==SOURCE and offline['source_unchanged'] and offline['native_images_unchanged']
 assert offline['session_configs_per_reference']==6 and offline['session_messages_per_reference_per_direction']==12
 for path,d in [(privatepath,private),(offlinepath,offline)]:
  assert s.sha(path.parent/'commands.json')==d['commands_sha256']
  for row in s.load(path.parent/'commands.json'):assert row.get('exit')==0 and 'exception' not in row and s.sha(row['log'])==row['log_sha256']
 fields=s.load(offline['local_value_fields_result']['path']);assert s.sha(offline['local_value_fields_result']['path'])==offline['local_value_fields_result']['sha256'] and fields['status']=='PASS_RETAINED_ORIGINAL_LOCAL_VALUE_VERSION_FIELDS'
 candidate=pathlib.Path(private['candidate']);assert candidate.is_relative_to(privatepath.parent) and s.sha(candidate/'BUILD_MANIFEST.json')==private['candidate_manifest_sha256'];new=v.bundle(candidate);old=v.bundle(N/'dist');assert s.sha(N/'dist/BUILD_MANIFEST.json')==private['old_dist_unchanged']==OLD_DIST
 oldmanifest=(N/'dist/BUILD_MANIFEST.json').read_bytes();lockpath=R/'migration/upstreams.lock.json';oldlock=lockpath.read_bytes();assert (N/'migration/upstreams.lock.json').read_bytes()==oldlock;lock=json.loads(oldlock);lockbefore=json.loads(oldlock)
 # Existing partial components remain partial; preserve every integration baseline.
 changed={'services.i2cp_initial_session','services.i2p_datagram','transport.azureus_volatile_database'}
 for c in lock['components']:
  if c['id'] not in changed:continue
  c['native_commits'].append({'repository':'https://github.com/d00rsfan/BiglyBTC.git','commit':SOURCE,'purpose':'integration'})
  c['review_records'].append((E/'REVIEW.md').as_posix());c['evidence'].append((E/'verification.json').as_posix())
  paths=([name for name in adoption['source_paths'] if any(part in name for part in ('/i2cp_channel/','/i2cp_session/','i2cp_session_config_tests'))] if c['id']=='services.i2cp_initial_session' else [name for name in adoption['source_paths'] if '/i2p_datagram/' in name] if c['id']=='services.i2p_datagram' else [name for name in adoption['source_paths'] if any(part in name for part in ('azureus_value_versions','azureus_db_runtime/','azureus_local_publish.inc','owned_azureus_service','owned_azureus_service_tests'))])
  for name in paths:
   if name not in c['target_paths']:c['target_paths'].append(name)
  for cov in c['source_coverage']:
   additions=['core/java/src/net/i2p/data/i2cp/SessionConfig.java','core/java/src/net/i2p/data/i2cp/CreateSessionMessage.java','core/java/src/net/i2p/data/i2cp/ReconfigureSessionMessage.java','core/java/src/net/i2p/client/impl/I2CPMessageProducer.java','router/java/src/net/i2p/router/client/ClientMessageEventListener.java'] if c['id']=='services.i2cp_initial_session' and cov['upstream_id']=='i2p-java' else ['core/src/com/biglybt/core/dht/control/impl/DHTControlImpl.java','core/src/com/biglybt/plugin/dht/impl/DHTPluginStorageManager.java','core/src/com/biglybt/plugin/dht/impl/DHTPluginImpl.java'] if c['id']=='transport.azureus_volatile_database' and cov['upstream_id']=='BiglyBT' else []
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
 for name in ('offline-initial.py','offline-session-pass-local-fail.py','finish-source-review.json','run.py','private.py','offline.py','finish.py','adopt.py','root-adoption.json','approved-inputs.json','i2cp-source-review.json','run-source-review.json','private-source-review.json','promotion.json'):copy(H/name,name)
 for leaf in (H/'offline-runs/r1').rglob('*'):
  if leaf.is_file():copy(leaf,'historical-helper-compile-failure/'+str(leaf.relative_to(H/'offline-runs/r1')))
 for leaf in (H/'amendments/java-frame-api').rglob('*'):
  if leaf.is_file():copy(leaf,'helper-correction/'+str(leaf.relative_to(H/'amendments/java-frame-api')))
 for leaf in (H/'offline-runs/r2').rglob('*'):
  if leaf.is_file() and leaf.suffix!='.class':copy(leaf,'actual-session-objects-and-local-helper-failure/'+str(leaf.relative_to(H/'offline-runs/r2')))
 failedfield=R/'build/azureus-local-publish-20261029/amendments/publication-authority/amendments/close-commit/oracle/runs/run-r2'
 for leaf in failedfield.iterdir():
  if leaf.is_file():copy(leaf,'historical-local-helper-fence-failure/'+leaf.name)
 for leaf in (H/'amendments/local-rsa-warmup').rglob('*'):
  if leaf.is_file() and 'runs' not in leaf.relative_to(H/'amendments/local-rsa-warmup').parts:copy(leaf,'local-helper-correction/'+str(leaf.relative_to(H/'amendments/local-rsa-warmup')))
 for lane in lanes:
  for leaf in (H/'runs'/(lane+'-r1')).iterdir():
   if leaf.is_file() and leaf.suffix in ('.json','.log','.xml'):copy(leaf,'runs/'+lane+'/'+leaf.name)
 for leaf in privatepath.parent.iterdir():
  if leaf.is_file() and leaf.suffix in ('.json','.log','.xml','.txt'):copy(leaf,'private/'+leaf.name)
 for leaf in offlinepath.parent.rglob('*'):
  if leaf.is_file() and leaf.suffix!='.class':copy(leaf,'offline/'+str(leaf.relative_to(offlinepath.parent)))
 fielddir=pathlib.Path(offline['local_value_fields_result']['path']).parent
 for leaf in fielddir.iterdir():
  if leaf.is_file():copy(leaf,'original-local-value-fields/'+leaf.name)
 for item in adoption['source_capsules']:
  cap=pathlib.Path(item['path']);label='i2cp' if 'i2cp-session' in str(cap) else 'local-publish';copy(cap,'source/'+label+'/manifest.json')
  m=s.load(cap)
  for name in m.get('artifacts',m.get('artifacts_sha256',{})):
   if name.startswith(('staging/','before/')):continue
   copy(cap.parent/name,'source/'+label+'/'+name)
  if label=='local-publish':
   for ancestor,label2 in [(cap.parent.parent.parent/'manifest.json','publication-authority-parent'),(R/'build/azureus-local-publish-20261029/manifest.json','initial-parent')]:copy(ancestor,'source/local-publish/'+label2+'.json')
 for item in adoption['source_reviews']:copy(item['path'],'source/reviews/'+pathlib.Path(item['path']).parent.name+'-'+pathlib.Path(item['path']).name)
 copy(N/'dist/BUILD_MANIFEST.json','dist-manifest.json');(out/'previous-dist-manifest.json').write_bytes(oldmanifest)
 verification={'status':'PASS_BOUNDED_SIGNED_I2CP_AND_LOCAL_DHT_PUBLICATION','source_commit':SOURCE,'source_paths':21,'lanes':lanes,'private_commands':7,'actual_session_configs_per_reference':6,'actual_session_messages_per_reference_per_direction':12,'original_local_value_version_cases':3,'original_local_value_cases':1,'original_filesystem_or_public_interoperability_claimed':False,'provider_rebuilt':False,'provider_sha256':private['provider_sha256'],'promotion':promotion,'artifacts_sha256':artifacts,'real_profile_used':False,'public_network_activated':False,'whole_feature_or_performance_parity_claimed':False};s.write(out/'verification.json',verification)
 (out/'REVIEW.md').write_text(f"""# Signed I2CP registration and local DHT publication

Implementation and qualified source `{SOURCE}` changes21 exact paths: signed-session8, local-publication11 and composed CMake2. GCC, Clang and leak-enabled ASan+UBSan each freshly built10 products and passed the same7 affected suites. The private runtime freshly ran all7 affected suites with actual private library resolution. No unrelated HTTP, queue, tracker or provider rebuild/replay. An initial Java fixture helper failed compilation by calling writeBytes on the I2CPMessage interface; a one-call writeMessage correction was run separately with no native rebuild/test replay, preserving that failed receipt/log. Both pinned official I2P2.13.0 and modified azi2pcore2.13.0.1 objects produced and verified12 Create/Reconfigure messages per direction across6 configs, with Unicode/canonical-preimage, maximum-size, auth/signature/stale controls. The original local-field helper initially printed all4 exact rows but failed its unchanged no-IO fence when the JDK RSA provider probed NativePRNG devices. Explicit JDK provider warmup before that fence fixed the helper; only that failed helper was rerun. Completed current-source session-object outputs were revalidated and reused without execution; the raw combined FAIL receipt and failed fence logs remain distinct. Actual original retained methods separately matched3 version reservations and1 local-value field projection. That helper uses explicitly stubbed persistence/fixed contact; it proves fields, never original filesystem or public interoperation.

SessionConfig uses original Destination + Java UTF16 sorted UTF8 Properties + date raw signatures (DSA0/Ed7, Enc0). The explicit numeric-loopback channel publishes a real CREATED ID and only acknowledges reconfiguration after matching UPDATED; original option merging retains omitted entries. It preserves single pending request, deadline, generation/revision and deletion/reentry guards. CREATED is registration only, with no tunnel/LeaseSet/readiness/delivery rights. TLS/auth, unsupported signing/encryption, offline options and remote/default router activation remain unsupported. Original router verifies create signature/date; its reconfigure handler only checks destination/ID and merges options. No reconfigure cryptographic admission claim.

DHT versions reserve original128 signed32 values durably in original dht[/netN]/version.dat under the same profile lock/OFD. Unknown fields survive; replacement/conflict/uncertain publication refuses without issuing a version. Local values carry actual bound contact, time and reserved version. One explicit admitted-peer command executes real token→STORE→readback with exact matching fields and current profile/cancel/deadline guards; final terminal publication is atomic and concurrent close wins. The remote receiver in this batch remains a generated volatile fixture with test-only closest facts, not a normal routed two-owner DHT server. Public bootstrap/receiving authority, maintenance/republish, full DDB, chat/ratings consumers remain open.

All states and upstream integration baselines remain unchanged;3 existing partial component records gain only this actual implementation/evidence. One checked development dist atomically replaced the previous tree, deleted immediately; test copies and temporary QtTest support were removed. No rollback build archive, real profile, public activation or full compatibility/performance claim.
""")
 shutil.copytree(out,N/E)
 for root in (R,N):
  target=root/'migration/upstreams.lock.json';assert target.read_bytes()==oldlock;pending=target.with_name(target.name+'.signed-publish-pending');assert not pending.exists()
  with pending.open('x') as stream:stream.write(json.dumps(lock,indent=2)+'\n');stream.flush();os.fsync(stream.fileno())
  os.replace(pending,target)
  log=H/('registry-validation-'+root.name+'.log')
  with log.open('wb') as stream:done=subprocess.run(['python3','-B',root/'migration/tools/validate.py'],cwd=root,env=dict(os.environ,GIT_NO_LAZY_FETCH='1',GIT_OPTIONAL_LOCKS='0'),stdout=stream,stderr=subprocess.STDOUT)
  assert done.returncode==0,log;copy_file(log,out/log.name);copy_file(log,N/E/log.name)
 print('PROMOTED_AND_RECORDED',SOURCE,promotion['manifest_sha256'],'fresh7/7/7 private7 bothJava12x2 +original4; olddistdeleted')
if __name__=='__main__':main()
