#!/usr/bin/env python3
"""Root-only closeout of the fresh mixed-tracker/I2CP cohort; no build or test reruns."""
import ctypes, datetime, hashlib, importlib.util, json, os, pathlib, re, shutil, subprocess
import xml.etree.ElementTree as ET
R=pathlib.Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';H=pathlib.Path(__file__).parent
E=pathlib.Path('migration/evidence/20261029-mixed-i2cp');out=R/E
SOURCE='ef494c64add015ea84df93ebcfbff1c7a38fa10b';OLD_DIST='64a35efc425f418ca8ae84ca5044db53c13144e2ad6666fcb88f4635835dd623'
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def load(p):return json.loads(pathlib.Path(p).read_bytes())
def write(p,v):pathlib.Path(p).write_text(json.dumps(v,indent=2)+'\n')
PINS={H/'run.py':'97c98dcb7faf1933ed787e254db29ba57c46d655af75b6ca9325c7c75ccce3f1',H/'root-adoption.json':'072504870cb231fdb211cafdda63cd2580eee7cb705981053b220e806a98869c',H/'private-focused/run.py':'f7fd607b4934574c42a7db6a3a0a866f1c0f477ca31b0d778cb83e84af13f536',H/'offline.py':'70db7a9fd2efdc4ab2dedd83142b2ba7161ff0789f68e118e72e67c582013af1',H/'i2cp-component-proposal.json':'e934bfafb3e48b99e44e58310fe00627256e0fc58885726702b5b0438a1dd001',H/'P07-02b-i2cp-initial-session.md':'ffe856f90a283899ef4e4a814f9e22324445b35dff225b6cb99c20315648479f'}
for p,pin in PINS.items():assert sha(p)==pin,'Frozen closeout input changed '+str(p)
spec=importlib.util.spec_from_file_location('private_mixed',H/'private-focused/run.py');v=importlib.util.module_from_spec(spec);spec.loader.exec_module(v)
for p,pin in v.PINS.items():assert sha(p)==pin
spec=importlib.util.spec_from_file_location('support',v.SUPPORT);s=importlib.util.module_from_spec(spec);spec.loader.exec_module(s)
source=s.source_snapshot(SOURCE);adoption=load(H/'root-adoption.json');assert adoption['source_commit']==SOURCE and adoption['source_path_count']==len(adoption['source_paths'])==10
for name,row in adoption['source_paths'].items():assert sha(N/name)==row['after_sha256']
SOURCES=[(R/'build/mainline-regular-mixed-tiers-20261029/manifest.json','c6d1e367a21e1e3f912caa711857c38d8d1ec9af5d5efc92d996c8f8ebc07eb1',R/'build/mainline-regular-mixed-tiers-20261029-review/manifest.json','b3dff9e86f5d054a20a9e6eba23a257674aba611725a8d14ae4fc854ffabd3e2'),(R/'build/i2cp-session-codec-20261029/amendments/allocation-failure-poison/manifest.json','96f471b5ebc464fa95dd350d39d98c4e68153aade0dc84e72acf2ea60a67f361',R/'build/i2cp-session-codec-20261029-review/amendments/allocation-failure-poison/manifest.json','ec2d856a2d509edf5635aa0309dbb14d450752b1dab0bf163c1a0477bed16127')]
for capsule,pin,review,review_pin in SOURCES:
 assert sha(capsule)==pin and sha(review)==review_pin
 for name,row in load(capsule)['source_paths'].items():assert row['after_sha256']==adoption['source_paths'][name]['after_sha256']
private_path=H/'private-focused/runs/r1/result.json';private=load(private_path)
assert private['status']=='PASS_PRIVATE_MIXED_TIERS_AND_HTTPS' and private['source_commit']==SOURCE and private['source_unchanged'] and len(private['actual_tests'])==3
plan=load(v.PLAN);provider=plan['provider']
for key in ('manifest','record','approval','review'):assert sha(provider[key]['path'])==provider[key]['sha256']
artifact=pathlib.Path(load(provider['manifest']['path'])['artifact']);assert sha(artifact)==private['provider_sha256']==provider['artifact_sha256']
lanes={};images={}
for lane in ('gcc','clang','san'):
 p=H/'runs'/(lane+'-r1');d=load(p/'result.json');wanted={'queue-policy','managed-queue','mainline-single-http','pure-i2cp-initial-session-codec'}|(set() if lane=='clang' else {'mainline-single-tracker-owner'})
 assert d['status']=='PASS_MIXED_I2CP_COHORT' and d['lane']==lane and d['source_commit']==SOURCE and d['source_unchanged'] and d['images_unchanged']
 assert d['actual_suites']==len(wanted) and set(d['suite_names'])==wanted and d['compile_only_suites']==(['mainline-single-tracker-owner'] if lane=='clang' else [])
 assert d['runner_sha256']==sha(H/'run.py') and d['source_receipt_sha256']==sha(H/'root-adoption.json') and load(p/'source-before.json')==source
 assert private['focused_results'][lane]['sha256']==sha(p/'result.json') and pathlib.Path(private['focused_results'][lane]['path']).resolve()==(p/'result.json').resolve()
 assert sha(p/'commands.json')==d['commands_sha256'];commands=load(p/'commands.json');assert [x['name'] for x in commands]==['build','http-loader','tests']
 for row in commands:assert row.get('exit')==0 and 'exception' not in row and sha(row['log'])==row['log_sha256']
 assert sha(p/'tests.xml')==d['tests_xml_sha256'];s.passed_xml(p/'tests.xml',wanted)
 assert all(x.get('status')=='run' for x in ET.parse(p/'tests.xml').getroot().iter('testcase'))
 assert sha(p/'ctest-full.log')==d['full_ctest_log_sha256'] and not re.search(r'^(?:SKIP\s*:|FAIL!)',(p/'ctest-full.log').read_text(),re.M)
 images[lane]=load(p/'images-before.json');assert len(images[lane])==8 and set(images[lane])==set(d['compiled_targets']);v.actual_suite_commands(p/'ctest-full.log',wanted,images[lane])
 for image in images[lane].values():assert sha(image['path'])==image['sha256'] and pathlib.Path(image['path']).stat().st_size==image['bytes']
 assert d['provider_sha256']==provider['artifact_sha256'];lanes[lane]={k:d[k] for k in ('actual_suites','suite_names','compile_only_suites','sanitizer_environment')}
assert set(private['copied_images'])=={'biglybtc-mainline-single-http-tests'}
for i,(target,selector,names) in enumerate(v.SELECTIONS,1):
 actual=private['actual_tests'][i-1];p=private_path.parent;assert actual['target']==target and actual['selector']==selector and actual['exit']==0 and sha(p/f'selected-{i}.xml')==actual['xml_sha256']
 rows=s.passed_xml(p/f'selected-{i}.xml');assert len(rows)==3 and {x.rsplit('::',1)[-1].removesuffix('()') for x in rows}=={'initTestCase','cleanupTestCase',*names}
 for path,pin in actual['initialized_images_sha256'].items():assert sha(path)==pin
assert sha(private_path.parent/'commands.json')==private['commands_sha256']
for command in load(private_path.parent/'commands.json'):assert command.get('exit')==0 and sha(command['log'])==command['log_sha256']
qpath=H/'offline-wrapper-r1/result.json';q=load(qpath);gcc=H/'runs/gcc-r1';identity=images['gcc']['biglybtc-i2cp-protocol-tests']
assert q['status']=='PASS_ACTUAL_I2CP_INITIAL_SESSION_OFFLINE' and q['source_commit']==SOURCE and q['cases_per_reference']==12 and q['source_unchanged'] and q['all_eight_images_unchanged'] and q['actual_gcc_image_count']==8
assert q['wrapper_sha256']==sha(H/'offline.py') and q['bridge_manifest_sha256']==SOURCES[1][1] and q['source_receipt_sha256']==sha(H/'root-adoption.json')
assert q['native_test_elf']==identity['path'] and q['native_test_elf_sha256']==identity['sha256'] and q['native_test_elf_bytes']==identity['bytes']
assert q['gcc_images_before_sha256']==sha(gcc/'images-before.json') and q['gcc_source_before_sha256']==sha(gcc/'source-before.json') and q['gcc_build_log_sha256']==sha(gcc/'build.log')
assert not q['connected_session'] and not q['router_activated'] and not q['native_compilation_executed'] and not q['full_native_cohort_pass_inferred']
assert q['exact_deterministic_exports_sha256']['official']==q['exact_deterministic_exports_sha256']['modified'] and len(q['exact_deterministic_exports_sha256']['official'])==12
for name,pin in q['files_sha256'].items():assert sha(H/'offline-r1'/name)==pin
for ref in ('official','modified'):
 assert q['actual_auth_options_refusal'][ref]['original_properties']==2 and q['actual_auth_options_refusal'][ref]['native_expected_error']=='Unsupported'
 assert q['actual_auth_options_refusal'][ref]['actual_refusal_assertion_source_sha256']==adoption['source_paths']['preview/native/tests/i2cp_protocol_tests.cpp']['after_sha256']
assert sha(qpath.parent/'commands.json')==q['commands_sha256'];offline_commands=load(qpath.parent/'commands.json');assert len(offline_commands)==1 and offline_commands[0]['name']=='offline' and offline_commands[0]['exit']==0 and sha(offline_commands[0]['log'])==offline_commands[0]['log_sha256']
assert not out.exists() and not (N/E).exists() and not (H/'promotion.json').exists()
old_lock=(R/'migration/upstreams.lock.json').read_bytes();assert old_lock==(N/'migration/upstreams.lock.json').read_bytes();lock=json.loads(old_lock);assert len(lock['upstreams'])==20 and len(lock['components'])==107
proposal=load(H/'i2cp-component-proposal.json');assert not any(c['id']==proposal['id'] for c in lock['components'])
for root in (R,N):assert not (root/'migration/tasks/P07-02b-i2cp-initial-session.md').exists()
candidate=pathlib.Path(private['candidate']);dist=N/'dist';assert sha(candidate/'BUILD_MANIFEST.json')==private['candidate_manifest_sha256'];new=v.bundle(candidate);old=v.bundle(dist);assert sha(dist/'BUILD_MANIFEST.json')==private['old_dist_unchanged']==OLD_DIST
for p in pathlib.Path('/proc').glob('[0-9]*/maps'):
 try:maps=p.read_text()
 except (OSError,PermissionError):continue
 assert str(dist)+'/' not in maps and str(candidate)+'/' not in maps and str(private_path.parent)+'/' not in maps,'Private/current preview still mapped by '+str(p)
libc=ctypes.CDLL(None,use_errno=True);exchange=libc.renameat2;exchange.argtypes=[ctypes.c_int,ctypes.c_char_p,ctypes.c_int,ctypes.c_char_p,ctypes.c_uint]
assert exchange(-100,os.fsencode(dist),-100,os.fsencode(candidate),2)==0,os.strerror(ctypes.get_errno());assert v.bundle(dist)==new and v.bundle(candidate)==old;shutil.rmtree(candidate)
removed=[]
for target,image in private['copied_images'].items():
 p=private_path.parent/target;assert sha(p)==image['sha256'];removed.append({'path':str(p),'sha256':sha(p)});p.unlink()
shutil.rmtree(private_path.parent/'test-support')
promotion={'status':'PROMOTED_ONE_CHECKED_DEVELOPMENT_DIST','source_commit':SOURCE,'manifest_sha256':sha(dist/'BUILD_MANIFEST.json'),'leaf_count':len(new['files_sha256']),'private_result_sha256':sha(private_path),'previous_dist_manifest_sha256':OLD_DIST,'previous_dist_removed':True,'previous_dist_retained':False,'atomic_exchange':True,'copied_test_images_removed':removed,'test_support_removed':True,'created_utc':datetime.datetime.now(datetime.timezone.utc).isoformat()};write(H/'promotion.json',promotion)
out.mkdir(parents=True,exist_ok=False);copied={}
def copy(p,name):
 p=pathlib.Path(p);assert p.is_file() and not p.is_symlink() and p.stat().st_size<=8*1024*1024
 dest=out/name;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,dest);copied[name]=sha(dest)
for name in ('root-adoption.json','run.py','offline.py','offline-source-review.json','offline-independent-source-review.json','promotion.json','finish.py','i2cp-component-proposal.json'):copy(H/name,name)
for capsule,pin,review,review_pin in SOURCES:
 label='mixed' if capsule==SOURCES[0][0] else 'i2cp';copy(capsule,'source/'+label+'/manifest.json');copy(review,'source/'+label+'/review-manifest.json');copy(review.parent/'review.json','source/'+label+'/review.json')
for label,base in [('mixed',SOURCES[0][0].parent),('i2cp',R/'build/i2cp-session-codec-20261029')]:copy(base/'source-pins.json' if label=='i2cp' else base/'original-source-pins.json','source/'+label+'/source-pins.json')
for name in ('manifest.json','review.json'):copy(R/'build/i2cp-session-codec-20261029-review'/name,'source/i2cp/initial-'+name) if (R/'build/i2cp-session-codec-20261029-review'/name).exists() else None
copy(SOURCES[1][0].parent/'interop/I2cpVectors.java','offline/I2cpVectors.java');copy(SOURCES[1][0].parent/'OFFLINE-COMMANDS.sh','offline/OFFLINE-COMMANDS.sh')
for lane in lanes:
 for name in ('result.json','commands.json','images-before.json','source-before.json','tests.xml','ctest-full.log','build.log','http-loader.log','tests.log'):copy(H/'runs'/(lane+'-r1')/name,'runs/'+lane+'-r1/'+name)
for name in ('run.py','source-review.json','manifest.json'):copy(H/'private-focused'/name,'private/'+name)
for p in private_path.parent.iterdir():
 if p.is_file() and p.suffix in ('.json','.xml','.txt','.log'):copy(p,'private/r1/'+p.name)
for p in qpath.parent.iterdir():
 if p.is_file() and p.suffix in ('.json','.log'):copy(p,'offline/wrapper/'+p.name)
for p in (H/'offline-r1').rglob('*'):
 if p.is_file() and not p.is_relative_to(H/'offline-r1/classes'):copy(p,'offline/run/'+p.relative_to(H/'offline-r1').as_posix())
copy(dist/'BUILD_MANIFEST.json','dist-manifest.json')
write(out/'verification.json',{'status':'PASS_BOUNDED_SAVED_MIXED_TRACKERS_AND_I2CP_CODEC','source_commit':SOURCE,'source_path_count':10,'lanes':lanes,'actual_private_runnable_rows':3,'actual_offline_cases_per_reference':12,'offline_native_lane':'gcc-only exact image; Clang/SAN pure units separately fresh','offline_result_sha256':sha(qpath),'provider_rebuilt':False,'provider_sha256':private['provider_sha256'],'dist_manifest_sha256':promotion['manifest_sha256'],'prior_dist_retained':False,'real_profile_used':False,'public_network_activated':False,'full_feature_or_performance_parity_claimed':False,'artifacts_sha256':copied})
(out/'REVIEW.md').write_text(f'''# Saved mixed tracker tiers and bounded I2CP framing

Implementation {SOURCE}; ten exact adopted paths: reviewed saved mixed-tier5, pure I2CP3 and CMake2. Current GCC/SAN passed five suites, Clang four; Clang full tracker-owner suite is compile-only. All eight products were source/image bound before and after; no historical PASS pooling. Three selected private rows exercised HTTP→UDP, UDP→HTTP and DIRECT HTTPS/JKS with actual private QtCore/Network/provider, and TLS/system OpenSSL for HTTPS. One candidate replaced exact prior64a dist atomically; old dist and copied test/QtTest support were removed.

Saved queued/unadmitted mixed/all-UDP tiers preserve original tier order/Java48 transient seed, per-URL response history, original selector and deadlines. Every2..8-member group is checked before IO; HTTP and numeric DIRECT UDP share one producer and full source/group/member authority. No payload read, hidden handle, peer/announce/DHT/PORT permission or timer is added. Active multi-URL selection, UDP DNS/proxy/advanced options and unsupported routes remain open.

The pure I2CP prerequisite preserves original5-byte framing/payload128KiB bounds and owns one frame at a time. Typed no-auth GetDate32, SetDate33, Disconnect30, SessionStatus20 and Destroy3 use explicit ASCII/nonnegative-date/status0..4 boundaries; zero date is the original null sentinel, never clock authority. Partial EOF/invalid framing and allocation failure poison the decoder until reset. Original readers may ignore trailers; native typed inputs conservatively reject them. No connected-session state machine, router, profile, credentials or transport is activated.

Actual offline bridge: one javac concrete-message helper; twelve deterministic cases each for exact official d76b and modified b086 JARs. Original writers/parsers and current GCC codec agreed on fields and exact frames; real GetDate two-property authentication was explicitly refused by the native subset. Actual class origins and unchanged source/eight-image graph are retained. This is GCC-only offline replay, distinct from fresh three-lane pure units; no prior datagram/identity comparison is relabeled or whole modified-core source equivalence claimed. Parent source review’s allocation invariant finding and its minimal correction are retained. Official9134 and plugin context33d97 remain separate; no broad P07/whole-client/performance completion. All old component baselines stay unchanged; new component remains in_progress with null integration hashes.
''')
for c in lock['components']:
 if c['id'] in ('engine.mainline_queued_scrape','engine.tracker_scrape_observations'):
  c['native_commits'].append({'repository':'https://github.com/d00rsfan/BiglyBTC.git','commit':SOURCE,'purpose':'integration'});c['review_records'].append((E/'REVIEW.md').as_posix());c['evidence'].append((E/'verification.json').as_posix())
lock['components'].append(proposal);assert len(lock['components'])==108
shutil.copytree(out,N/E) # All referenced evidence exists in both repositories before registry publication.
prepared=[]
for root in (R,N):
 shutil.copyfile(H/'P07-02b-i2cp-initial-session.md',root/'migration/tasks/P07-02b-i2cp-initial-session.md')
 target=root/'migration/upstreams.lock.json';assert target.read_bytes()==old_lock,'Registry preimage changed'
 pending=target.with_name(target.name+'.mixed-i2cp-pending')
 with pending.open('x') as stream:stream.write(json.dumps(lock,indent=2)+'\n');stream.flush();os.fsync(stream.fileno())
 prepared.append((target,pending))
for target,pending in prepared:
 assert target.read_bytes()==old_lock,'Registry preimage changed before replacement';os.replace(pending,target)
for label,root in [('R',R),('N',N)]:
 log=H/('registry-validation-'+label+'.log')
 with log.open('wb') as stream:completed=subprocess.run(['python3','-B',root/'migration/tools/validate.py'],cwd=root,env=dict(os.environ,GIT_NO_LAZY_FETCH='1',GIT_OPTIONAL_LOCKS='0'),stdout=stream,stderr=subprocess.STDOUT)
 assert completed.returncode==0 and 'PASS registry: 20 sources, 108 components' in log.read_text(),'Registry validator failed '+label
 shutil.copyfile(log,out/log.name);shutil.copyfile(log,N/E/log.name)
print('PROMOTED_AND_RECORDED',SOURCE,promotion['manifest_sha256'],'fresh5/4/5, private3, offline12/reference GCC-only; both20/108 validators PASS; prior64a deleted')
