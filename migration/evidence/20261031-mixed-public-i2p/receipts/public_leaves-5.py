#!/usr/bin/env python3
"""Validate measured chapter inputs; stage metadata only under this task. Never apply/git-write/publish."""
import argparse,copy,datetime,hashlib,importlib.util,json,pathlib,re,subprocess,sys
H=pathlib.Path(__file__).resolve().parent;T=pathlib.Path('/home/yurii/Java/BiglyBT/build/mixed-network-20261031');R=T.parents[1];N=R.parent/'BiglyBTC'
E='migration/evidence/20261031-mixed-public-i2p';TASK='migration/tasks/P07-mixed-network-torrent.md'
UPDATE='migration/updates/services.i2cp_initial_session/20261031-mixed-public-i2p.md'
COMPONENTS=('services.i2cp_initial_session','engine.selected_swarm_admission','engine.queue_policy','ui.classic_statistics_preview')
ACTUAL='PASS_REAL_ISOLATED_ROUTER_SINGLE_TORRENT_PUBLIC_I2P_MIXED'
PREVIEW_HELPER_SHA='ca817d16910d030ef2d1496f7318045b1aee808882881c90a2c2b3aa1e3fc64b'
PUBLIC_PROVIDER_SHA='3cf1c941d3d4632588544ea8cca2d65a16a14ae857cae579fe10d0439dd5e398'
DEFAULT='aad928753cb8f2dac8f99245298d987cfe2c4bee9e7bdebf5b8d7645ed6d559b'
def sha(p):
 h=hashlib.sha256()
 with pathlib.Path(p).open('rb') as f:
  while data:=f.read(1024*1024):h.update(data)
 return h.hexdigest()
def load(p):return json.loads(pathlib.Path(p).read_bytes())
def write(p,v):p.parent.mkdir(parents=True,exist_ok=True);p.write_text(json.dumps(v,indent=2)+'\n')
def git(root,*a):return subprocess.check_output(['git','-C',str(root),*a],text=True,timeout=30).strip()
def checked(row):
 p=pathlib.Path(row['path']);assert p.is_absolute() and p.is_file() and not p.is_symlink()
 assert re.fullmatch('[0-9a-f]{64}',row['sha256']) and sha(p)==row['sha256'],p
 return p

def original():
 p=pathlib.Path(git(R,'rev-parse','--git-path','index'));p=p if p.is_absolute() else R/p
 return {'head':git(R,'rev-parse','HEAD'),'index_sha256':sha(p)}
def immutable_registry(v):
 v=copy.deepcopy(v)
 for c in v['components']:
  if c['id'] in COMPONENTS:
   for k in ('native_commits','evidence','task_records','review_records','target_paths'):c.pop(k,None)
 return v

def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--inputs',type=pathlib.Path,required=True);ap.add_argument('--output',type=pathlib.Path,required=True);ap.add_argument('--ci-test-guard',type=pathlib.Path,required=True);ap.add_argument('--ci-test-guard-sha256',required=True);a=ap.parse_args()
 out=a.output.resolve();assert out.is_relative_to(T) and out!=T and not out.exists(),'Fresh task-local staging only'
 input_sha=sha(a.inputs);s=load(a.inputs);code=s['native_source_commit'];tree=s['native_source_tree']
 assert re.fullmatch('[0-9a-f]{40}',code) and re.fullmatch('[0-9a-f]{40}',tree)
 assert git(N,'rev-parse',code+':preview/native')==git(N,'rev-parse','HEAD:preview/native')==tree
 assert not git(N,'status','--porcelain','--untracked-files=all','--','preview/native')
 baseline=original();assert baseline==s['original_preservation']
 refs=s['receipts'];required={'source_map','gcc','san','default','gcc_actual','san_actual','provider_record','provider_source','provider_review','role_approval','runtime_manifest','ci','preview','preservation','cleanup'}
 assert required<=refs.keys() and all(re.fullmatch('[a-z][a-z0-9_-]{0,63}',k) for k in refs)
 paths={k:checked(row) for k,row in refs.items()};v={k:load(p) for k,p in paths.items()};files=v['source_map']
 assert files and set(files)==set(git(N,'ls-tree','-r','--name-only',code,'--','preview/native').splitlines())
 assert all(name.startswith('preview/native/') and sha(N/name)==h for name,h in files.items())
 # Reuse the already source-reviewed inert preview predicates; no main/subject
 # is invoked. Their actual receipt checks are also used before bundle staging.
 helper=pathlib.Path('/home/yurii/Java/BiglyBT/build/mixed-network-20261031/sdk-accept-repin-final/stage-preview.py');assert sha(helper)==PREVIEW_HELPER_SHA
 sys.dont_write_bytecode=True;spec=importlib.util.spec_from_file_location('mixed_closeout_receipt_checks',helper);b=importlib.util.module_from_spec(spec);spec.loader.exec_module(b)
 for lane,key in (('private','gcc'),('san','san')):
  b.checked_build(paths[key],refs[key]['sha256'],lane,code,tree)
  assert v[key]['source_map_sha256']==refs['source_map']['sha256']
  assert len(v[key]['expected_tests'])==25
 d=v['default'];assert d['status']=='PASS' and d['lane']=='default' and d['source_commit']==code and d['source_tree']==tree and len(d['expected_tests'])==20
 assert d['source_map_sha256']==refs['source_map']['sha256']
 assert d['commands'] and all(x['exit_code']==0 and not x.get('timed_out',False) and sha(x['log'])==x['log_sha256'] for x in d['commands'])
 assert all(sha(row['path'])==row['sha256'] and pathlib.Path(row['path']).stat().st_size==row['bytes'] for row in d['images'].values())
 actuals={}
 for lane,kind in (('gcc','private'),('san','san')):
  actuals[lane]=b.checked_acceptance(paths[lane+'_actual'],refs[lane+'_actual']['sha256'],kind,code,tree,refs['gcc']['sha256'])
  inputs=load(paths[lane+'_actual'].parent/'inputs.json');assert inputs['build_receipt_sha256']==refs[lane]['sha256']
 assert paths['gcc_actual'].parent!=paths['san_actual'].parent
 approval=v['role_approval'];assert approval['schema']=='BIGLYBTC_MIXED_I2P_ROLE_APPROVAL_V1' and approval['status']=='ROOT_APPROVED_MIXED_I2P_ROLES' and approval['shipping_approved'] is False
 for key,name in (('runtime_manifest_sha256','runtime_manifest'),('source_build_record_sha256','provider_record'),('source_capsule_sha256','provider_source'),('review_sha256','provider_review')):assert approval[key]==refs[name]['sha256']
 assert v['provider_record']['schema']=='BIGLYBTC_MIXED_I2P_SOURCE_RECORD_V1' and v['provider_record']['source_capsule_sha256']==refs['provider_source']['sha256']
 assert v['provider_record']['artifact_sha256']==approval['artifact_sha256'] and len(v['provider_record']['required_exports'])==39
 assert v['provider_record']['actual_controls']['status']=='PASS_MIXED_SDK_DIRECT_CONTROLS_NOT_APPROVED'
 ci=v['ci'];assert ci['status']=='PASS'
 # Local qualified source/images remain90; CI alone runs its exact one-test child.
 assert a.ci_test_guard.is_absolute() and a.ci_test_guard_sha256=='627f250c247e9fe3a9595bd3268847e2d532b0dd8e8d04946c88c6fc0d64b719'==sha(a.ci_test_guard)
 guard=load(a.ci_test_guard);leaf='preview/native/tests/i2p_classic_host_tests.cpp'
 assert guard['basis_commit']==code=='90f1d3defd31acacb784cb8cda40d8d06ba6d76d' and set(guard['source_paths'])=={leaf}
 row=guard['source_paths'][leaf];assert row['before_sha256']=='83e1b11599cbba8dcd0d31a00f4f4b98f544042301e10dc414073d19fc42d2a3' and row['after_sha256']=='f9bdf2c4ce1db44c0f9ce40794d4722bb0eadeb87cb0eff66fa58594052ef092'
 ci_code=ci['source_commit'];assert ci_code=='89a42691ac2f7193b6fd95cbf23a8dbd172e7477' and re.fullmatch('[0-9a-f]{40}',ci_code) and ci_code!=code
 assert git(N,'rev-list','--parents','-n','1',ci_code).split()==[ci_code,code]
 assert git(N,'diff','--name-only','--no-renames',code,ci_code,'--').splitlines()==[leaf]
 for commit,key in ((code,'before_sha256'),(ci_code,'after_sha256')):
  raw=subprocess.check_output(['git','-C',str(N),'show',commit+':'+leaf]);assert hashlib.sha256(raw).hexdigest()==row[key]
 ci_delta={'qualified_source_commit':code,'qualified_source_tree':tree,'ci_source_commit':ci_code,'ci_source_tree':git(N,'rev-parse',ci_code+':preview/native'),'test_path':leaf,'before_sha256':row['before_sha256'],'after_sha256':row['after_sha256'],'capsule_sha256':a.ci_test_guard_sha256,'all_other_repository_files_identical':True,'local_test_guard_rebuilt':False}
 assert len(ci['runs'])==2 and all(x['status']=='completed' and x['conclusion']=='success' and x['headSha']==ci['source_commit'] for x in ci['runs'])
 assert {x['workflowName'] for x in ci['runs']}=={'Native desktop preview unit and smoke tests','Native plugin SDK contract'}
 bundles={k:checked(row) for k,row in s['bundle_manifests'].items()};assert set(bundles)=={'default','private'} and s['bundle_manifests']['default']['sha256']==DEFAULT
 assert v['preview']['status']=='PASS_STAGED_OPTIONAL_MIXED_PREVIEW' and v['preview']['source_commit']==code and v['preview']['source_tree']==tree and v['preview']['mapped_files']==119 and v['preview']['shipping_approved'] is False
 assert v['preview']['manifest_sha256']==s['bundle_manifests']['private']['sha256'] and v['preview']['build_receipt_sha256']==refs['gcc']['sha256']
 assert v['preview']['role_approval_sha256']==refs['role_approval']['sha256'] and v['preview']['provider_sha256']==approval['artifact_sha256']
 assert all(b.inventory(path.parent)==load(path)['files_sha256'] for path in bundles.values())
 preserve=v['preservation'];assert preserve['status']=='PASS_ORIGINAL_STATE_AND_PREVIEW_BOUNDARIES'
 assert preserve['original_head']==baseline['head'] and preserve['original_index_sha256']==baseline['index_sha256']
 assert preserve['native_source_commit']==code and preserve['native_source_tree']==tree
 assert preserve['default_manifest_sha256']==DEFAULT and preserve['optional_manifest_sha256']==s['bundle_manifests']['private']['sha256']
 assert preserve['tmp_biglybtc_entry_count']==0 and v['cleanup']['current_tmp_biglybtc_count']==0 and v['cleanup']['deployment_cleanup_now_automatic']
 registry=load(R/'migration/upstreams.lock.json');assert registry==load(N/'migration/upstreams.lock.json')
 assert [len(registry[k]) for k in ('upstreams','components','external_dependencies')]==[20,108,22]
 keep=immutable_registry(registry);commits=s.get('native_commits',[code]);assert code in commits
 assert set(s['component_targets'])==set(COMPONENTS) and all(s['component_targets'].values())
 for cid in commits:
  assert re.fullmatch('[0-9a-f]{40}',cid) and git(N,'rev-parse',cid+'^{commit}')==cid
  subprocess.run(['git','-C',str(N),'merge-base','--is-ancestor',cid,code],check=True)
 for c in registry['components']:
  if c['id'] not in COMPONENTS:continue
  for cid in commits:
   if not any(row['commit']==cid for row in c['native_commits']):c['native_commits'].append({'repository':'https://github.com/d00rsfan/BiglyBTC.git','commit':cid,'purpose':'integration'})
  for key,value in (('evidence',E+'/verification.json'),('task_records',TASK),('review_records',UPDATE)):
   if value not in c[key]:c[key].append(value)
  for target in s.get('component_targets',{}).get(c['id'],[]):
   assert target in files
   if target not in c['target_paths']:c['target_paths'].append(target)
 assert immutable_registry(registry)==keep,'States/source coverage/nulls/baselines/dependencies changed'
 out.mkdir(mode=0o700);e=out/E;index={}
 def copy_public(label,row,destination=None):
  assert re.fullmatch('[a-z][a-z0-9_-]{0,63}',label);p=checked(row)
  assert p.is_relative_to(R) or p.is_relative_to(N)
  assert p.stat().st_size<=2*1024*1024 and (p.suffix in ('.json','.py','.hpp','.cpp','.java','.txt','.md','.patch','.cmake') or p.name in ('COPYING','LICENSE'))
  text=p.read_text(encoding='utf-8');assert '\0' not in text
  assert not any(x in {'router','peer-data','seed-profile','leech-profile','home','classes','seed-payload','leech-payload'} for x in p.relative_to(R if p.is_relative_to(R) else N).parts)
  dst=e/(destination or ('receipts/'+label+p.suffix));assert not dst.exists();dst.parent.mkdir(parents=True,exist_ok=True);dst.write_bytes(p.read_bytes());index[label]={**row,'bytes':p.stat().st_size,'copy':str(dst.relative_to(out))}
 for name,row in refs.items():copy_public(name,row)
 for group in ('history','public_leaves'):
  for i,row in enumerate(s.get(group,[])):copy_public(group+'-'+str(i),row)
 if 'provider_public_evidence' in refs:
  assert refs['provider_public_evidence']['sha256']==PUBLIC_PROVIDER_SHA
  public=v['provider_public_evidence'];assert public['status']=='SOURCE_ONLY_NO_RUNTIME_AUTHORITY' and not public['contains_binaries'] and not public['contains_profile_keys']
  assert public['source_capsule_sha256']==refs['provider_source']['sha256'] and public['source_build_record_sha256']==refs['provider_record']['sha256']
  for i,(name,h) in enumerate(public['artifacts_sha256'].items()):
   rel=pathlib.PurePosixPath(name);assert not rel.is_absolute() and '..' not in rel.parts
   copy_public('provider-'+str(i),{'path':str(paths['provider_public_evidence'].parent/name),'sha256':h},'provider-public-evidence/'+name)
 verification={'status':'PASS_BOUNDED_SINGLE_TORRENT_PUBLIC_I2P_MIXED_CHAPTER','source_commit':code,'source_tree':tree,'receipts':refs,'actual_runs':actuals,'original_preservation':baseline,'bundle_manifests':s['bundle_manifests'],'registry_counts':{'upstreams':20,'components':108,'dependencies':22},'whole_client_state':'in_progress','shipping_approved':False,'stop_after_chapter':True,'next_chapter_started':False,'private_keys_published':False}
 verification['ci_test_only_delta']=ci_delta
 copy_public('ci-test-guard',{'path':str(a.ci_test_guard),'sha256':a.ci_test_guard_sha256})
 for leaf in ('changes.patch','failure-excerpt.txt'):
  q=a.ci_test_guard.parent/leaf;copy_public('ci-test-guard-'+('patch' if leaf.endswith('.patch') else 'failure'),{'path':str(q),'sha256':guard['artifacts_sha256'][leaf]})
 write(e/'verification.json',verification);write(e/'receipt-index.json',index)
 next_step='Propose I2P peer discovery and magnets beyond tracker-only peers and explicitly warmed known local LS2 caches; do not begin it.'
 summary=f'''Native `{code}` (preview tree `{tree}`) qualifies the bounded mixed Public+I2P single-torrent chapter. A mixed row has one Public torrent handle, payload/piece/storage/checkpoint owner and queue slot. Key0 mixed transport borrows that owner; pure-I2P key1 remains separate. Explicit original network order/unknown metadata, source fences, independent lifecycle, shared queue/rates and cumulative payload counters remain preserved. Whole client/P07 and larger components remain in progress.

Current GCC/SAN25 and default20 selected CTests plus actual hosted SDK/desktop success are indexed. Fresh GCC and leak-enabled native ASan+UBSan isolated router fixtures prove complementary Public/I2P payload ranges, exact BT privacy-ID relation, route retry, Public continuation after I2CP EOF, cross-target controls, original saved pause/restart, joined disk/inode/counter checks and cleanup. SDK/Qt/Java are not instrumented by Native sanitizers. Retained failed attempts remain failures; no stability, throughput or full-parity claim follows.

Explicit opt-in uses the separately approved39-export SDK/source/artifact family. Source capability checks and same-engine opaque origins fence actual retained handles; no second ManagedStorage or direct DNS fallback is created. Optional119-file preview is updated under its measured manifest; default `{DEFAULT}` remains untouched and shipping approval stays false. Scope is isolated loopback with genuinely signed, explicitly warmed local LS2 caches. Internet bootstrap/discovery, DHT/magnets, remote/multihop tunnels, Tor and full plugin/backend/performance parity remain open.

Original source baselines: BiglyBT `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`; azneti2phelper `33d97eca67a9f332e3d4da7e7492ec54f4bec078`; official I2P `9134f808337b401e8e53c73734c81fab04280c9d`; libtorrent source parent `740a0b9aeabe00e762cc0efe4a0f27593db2550b`. The actual modified azi2pcore JAR SHA256 `b0866aa843a55d9290755c7a5cc3091cda765374544914afb4d3557ae99994b5` is distinct from official I2P source. SDK capsule `{refs['provider_source']['sha256']}`, build record `{refs['provider_record']['sha256']}`, artifact `{approval['artifact_sha256']}`, role approval `{refs['role_approval']['sha256']}` and review `{refs['provider_review']['sha256']}` are exact independent inputs, not repository integration claims.

Original Java HEAD/index/live profile and20 upstream/108 component/22 dependency registry identities, states, source coverage, integrated/null baselines are preserved. Only actual Native commit/task/review/evidence/target references are appended to four existing affected components. No logs/binaries/profiles/keys or full SDK trees are copied; explicit small public leaves and supplied reproducible provider text evidence are indexed.

STOP after this chapter. Next proposed only: {next_step}
'''
 summary+=f'\nHosted CI ran exact test-only child `{ci_code}`. Its sole repository difference from qualified executable source `{code}` is the reviewed unsupported-dependency skip in `i2p_classic_host_tests.cpp` (`83e1b115` to `f9bdf2c4`). Every production byte is identical. Local GCC/SAN/default test and application images, actual transfers and optional preview remain qualified at `{code}`; the guarded test image was not rebuilt locally. The original failed hosted test is retained as a failure.\n'
 source_rows='\n'.join('| '+c['id']+' | '+str(c['state'])+' | unchanged |' for c in registry['components'] if c['id'] in COMPONENTS)
 review='# One-torrent Public + I2P mixed chapter\n\n'+summary+'\n| Existing component | State | Source coverage / integration fields |\n| --- | --- | --- |\n'+source_rows+'\n\n[Verification](verification.json); [explicit public receipt index](receipt-index.json).\n'
 docs={E+'/REVIEW.md':review,TASK:'# Mixed Public and I2P torrent\n\nBounded chapter complete; stopped. '+summary.split('\n\n')[0]+f'\n\n[Review](../evidence/20261031-mixed-public-i2p/REVIEW.md).\n\nNext proposed only: '+next_step+'\n',UPDATE:f'# Mixed Public and I2P one-owner integration\n\nUpstream repositories: https://github.com/BiglySoftware/BiglyBT.git and https://github.com/BiglySoftware/BiglyBT-plugin-azneti2phelper.git. Selected old A and candidate B remain the same pinned committed baselines: BiglyBT cb8c6cb9bb97916cbc7e80338a017200ffc3e71f and helper33d97eca67a9f332e3d4da7e7492ec54f4bec078. No upstream-update or integrated-through advancement is claimed. Native integration commits: '+', '.join(commits)+f'.\n\nImplemented bounded source/privacy/routing/storage/UI behavior and explicit remaining discovery/performance gaps are in the [review](../../evidence/20261031-mixed-public-i2p/REVIEW.md). Exact local/hosted commands and independent root/source reviews are linked by the verification/index. Existing component states and source/null/baseline fields are unchanged.\n','migration/ACTIVE.md':'# Mixed Public and I2P torrent complete; stopped\n\n'+summary.split('\n\n')[0]+f'\n\n[Review](evidence/20261031-mixed-public-i2p/REVIEW.md).\n\nNext proposed only: {next_step}\n\nPreserve Java/live profile and manifests; [resource policy](RESOURCE_POLICY.md). Work:build/mixed-network-20261031.\n','migration/STATE.md':f'# Current native migration state\n\nBounded mixed one-torrent chapter qualified at `{code}`. [Review](evidence/20261031-mixed-public-i2p/REVIEW.md). Whole client/P07 in progress; STOP. I2P discovery beyond tracker/warmed local-cache scope remains open.\n'}
 plan=(R/'MIGRATION_PLAN.md').read_text().splitlines();assert plan[2].startswith('Latest user boundary') and plan[4].startswith('Qualified native source')
 plan[2]='Latest user boundary (2026-10-31): mixed Public+I2P single-torrent chapter complete; STOP. Propose I2P discovery next without starting it. Preserve running Java/live profile and main-only milestones.'
 plan[4]=f'Qualified native source `{code}`: one mixed torrent/storage/queue owner with genuine Public+I2P contribution, source-bound key0 route, independent controls and original counters/restart. [Review]({E}/REVIEW.md). Whole client/P07 remain in progress; discovery beyond tracker/warmed local-cache scope is open.'
 text='\n'.join(plan)+'\n';marker='## 2026-10-31 — Mixed Public and I2P torrent (authorised, in progress)';assert marker in text;start=text.index(marker);end=text.find('\n## ',start+len(marker));end=len(text) if end<0 else end
 docs['MIGRATION_PLAN.md']=text[:start]+'## 2026-10-31 — Mixed Public and I2P torrent (bounded chapter complete)\n\n'+plan[4]+'\n\nSTOP. Proposed next: '+next_step+'\n'+text[end:]
 matrix=(R/'migration/FEATURE_MATRIX.md').read_text().splitlines();assert sum(x.startswith('| F06.i2cp_initial_session |') for x in matrix)==1
 docs['migration/FEATURE_MATRIX.md']='\n'.join(f'| F06.i2cp_initial_session | Signed I2CP, datagrams, tracker and bounded Streaming/provider owners | [Session](tasks/P07-02b-i2cp-initial-session.md) | In progress; `{code}` qualifies one-owner mixed Public+I2P controls/counters/restart under warmed local-cache/tracker scope. I2P discovery/magnets/full-client/plugin/performance parity remain open. [Review](evidence/20261031-mixed-public-i2p/REVIEW.md). |' if x.startswith('| F06.i2cp_initial_session |') else x for x in matrix)+'\n'
 docs['migration/UPSTREAM_TRACKING.md']=(R/'migration/UPSTREAM_TRACKING.md').read_text()+f'\n## 2026-10-31 bounded mixed Public and I2P one-owner integration\n\nNative `{code}` preserves one storage/checkpoint/queue owner while source-bound key0 and pure key1 identities remain separate. [Review](evidence/20261031-mixed-public-i2p/REVIEW.md) records full original/SDK hashes, measured qualification and unsupported discovery limits. Registry states/source coverage/null baselines remain unchanged; whole client/P07 in progress. STOP; next proposal only is I2P discovery/magnets beyond warmed-cache/tracker scope.\n'
 before={}
 for name,text in docs.items():
  before[name]=sha(R/name) if (R/name).exists() else None;dst=out/name;dst.parent.mkdir(parents=True,exist_ok=True);dst.write_text(text)
 write(out/'migration/upstreams.lock.json',registry);before['migration/upstreams.lock.json']=sha(R/'migration/upstreams.lock.json')
 c=load(R/'migration/continuation.json');c.update(updated_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),objective='Authorised mixed one-torrent Public+I2P chapter complete; stop.',qualified_native_source=code,native_head=git(N,'rev-parse','HEAD'),current_section='Mixed Public+I2P single torrent — complete',current_source_qualification=verification['status'],stop_after_current_milestone=True,stop_after_current_section=True,next_action='STOP. '+next_step,active_sessions=[],active_local_sessions=[],pending_question='None.',effective_acceptance=E+'/verification.json',remote_ci=ci,accepted_dist_sha256=DEFAULT)
 c.update(native_source_pushed=True,whole_client_state='in_progress',effective_native_recipe='build/mixed-network-20261031/run-batch.py; exact committed current-source receipts indexed in verification')
 c['notes']='Bounded mixed one-owner chapter complete; whole client/P07 remain incomplete. Original Java/live profile/default dist and registry baselines preserved. Explicit optional preview remains shippingfalse/warmed-cache scope; no discovery/DATA/chat chapter started.'
 c.setdefault('receipts',[]).append({'name':'Mixed Public and I2P one-owner chapter','path':E+'/verification.json','sha256':sha(e/'verification.json')});write(out/'migration/continuation.json',c);before['migration/continuation.json']=sha(R/'migration/continuation.json')
 assert sha(a.ci_test_guard)==a.ci_test_guard_sha256
 assert sha(a.inputs)==input_sha and all(sha(path)==refs[key]['sha256'] for key,path in paths.items()) and original()==baseline
 assert all(sha(path)==s['bundle_manifests'][key]['sha256'] and b.inventory(path.parent)==load(path)['files_sha256'] for key,path in bundles.items())
 assert all(sha(R/name)==h for name,h in before.items() if h is not None)
 assert immutable_registry(load(R/'migration/upstreams.lock.json'))==immutable_registry(load(N/'migration/upstreams.lock.json'))==keep
 staged={str(path.relative_to(out)):sha(path) for path in out.rglob('*') if path.is_file()}
 write(out/'staging-receipt.json',{'status':'READY_FOR_ROOT_REVIEW_NOT_APPLIED','source_commit':code,'source_tree':tree,'inputs_sha256':input_sha,'before_docs_sha256':before,'staged_files_sha256':staged,'registry_mirror_target':str(N/'migration/upstreams.lock.json'),'registry_mirror_sha256':sha(out/'migration/upstreams.lock.json'),'canonical_or_native_mutated':False,'publication_executed':False,'stop_after_chapter':True});print(out/'staging-receipt.json')
if __name__=='__main__':main()
