"""Collect finite, public evidence only after all real gates have passed."""
import hashlib,json,pathlib,shutil
T=pathlib.Path(__file__).resolve().parent;R=T.parents[1];C=T/'provider-candidate-r2';N=T/'native-followup-20261030/amendments/live-executor-sdk';A=T/'final-transfer-live-executor';E=T/'closeout-final'
sha=lambda p:hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
load=lambda p:json.loads(pathlib.Path(p).read_bytes())
def write(p,d):p.parent.mkdir(parents=True,exist_ok=True);p.write_text(json.dumps(d,indent=2)+'\n')
def cp(source,destination):
 assert source.is_file() and not source.is_symlink(),source
 destination.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,destination);assert sha(source)==sha(destination)
def top(source,destination):
 for f in source.iterdir():
  if f.is_file() and f.suffix in {'.json','.xml','.log','.txt','.py','.mk','.cmake','.patch','.cpp','.hpp','.md'}:cp(f,destination/f.name)
gates=load(T/'finish-gates-r3/result.json');assert gates['status']=='PASS_SDK_NATIVE_AND_REAL_TRANSFER_GATES'
assert gates['source_commit']=='9f46cf05d133624e48c6d60d45d2c503a6141ff9'
record=load(C/'source-build-record.json');assert record['actual_controls']['actual_ctest_suites']==9
assert not E.exists();shutil.copytree(T/'closeout-staging',E/'history')
for rel,pin in record['evidence_sha256'].items():
 f=C/rel;assert sha(f)==pin
 assert f.suffix in {'.json','.xml','.log','.txt','.cmake','.cpp'},rel
 cp(f,E/'sdk'/rel)
for rel in ('source-build-record.json','root-private-owner-approval.json','approval-proposal/private-i2p-owner-runtime.json'):
 cp(C/rel,E/'sdk'/rel)
top(C/'root-artifact-review',E/'sdk/root-artifact-review')
for attempt in ('r1','r2'):
 for leaf in ('result.json','commands.json','provider-tests.xml','root-owned-test-stop.json','root-cache-diagnosis.json'):
  f=C/'runs'/attempt/leaf
  if f.is_file():cp(f,E/'sdk/history'/attempt/leaf)
for name in ('shutdown-storage-r2','sdk-r2-test-repair','sdk-r2-configured-continuation'):
 top(T/'provider-recipe'/name,E/'sdk/recipes'/name)
shutil.copytree(T/'public-provider-qualified',E/'provider')
lanes={}
for lane in ('clang','gcc','san','default'):
 out=N/('runs/'+lane+'-r1');result=load(out/'result.json');expected=4 if lane=='default' else 11
 assert result['actual_suites']==expected and result['source_commit']==gates['source_commit']
 assert result['status']==('PASS_DEFAULT_I2P_ATTACHMENT_CONTROLS' if lane=='default' else 'PASS_PRIVATE_I2P_OWNER_AFFECTED_COHORT')
 top(out,E/'native'/lane);lanes[lane]={'actual_suites':expected,'result_sha256':sha(out/'result.json')}
top(N,E/'native/recipe')
actual=A/'runs/r1';result=load(actual/'result.json');assert result['status']=='PASS_REAL_ISOLATED_ROUTER_PROVIDER_TRANSFER'
for path,pin in load(actual/'public-artifacts.json').items():
 f=pathlib.Path(path);assert f.parent==actual and sha(f)==pin;assert f.name not in ('seed.keys','leech.keys');cp(f,E/'acceptance/run'/f.name)
for leaf in ('public-artifacts.json','private-cleanup.json'):cp(actual/leaf,E/'acceptance/run'/leaf)
assert load(actual/'private-cleanup.json')['owned_private_fixture_paths_removed']
top(A,E/'acceptance/source')
diagnostic=T/'diagnostics/tracker-state-observation/runtime/runs/r2';d=load(diagnostic/'diagnostic-result.json')
for path,pin in d['public_artifacts'].items():
 f=pathlib.Path(path);assert f.parent==diagnostic and sha(f)==pin;cp(f,E/'diagnostics'/f.name)
cp(diagnostic/'diagnostic-result.json',E/'diagnostics/diagnostic-result.json')
for folder,name in [(T/'diagnostics/tracker-state-observation','tracker-observation'),(T/'implementation/amendments/outgoing-unspecified-port','outgoing-port-zero')]:top(folder,E/'fixes'/name)
for name in ('finish-gates-r3.py','collect-final.py'):cp(T/name,E/'recipes'/name)
top(T/'finish-gates-r3',E/'gate-receipts')
verification={'schema':1,'status':'PASS_BOUNDED_PRIVATE_I2P_OWNER_TRANSFER','qualified_source_commit':gates['source_commit'],'upstream_java_commit':'cb8c6cb9bb97916cbc7e80338a017200ffc3e71f','upstream_libtorrent_commit':record['upstream_commit'],'provider_source_capsule_sha256':record['source_capsule_sha256'],'provider_artifact_sha256':record['artifact_sha256'],'provider_artifact_bytes':record['artifact_bytes'],'provider_sdk_tree_sha256':record['sdk_tree_sha256'],'provider_actual_fresh_suites':9,'native_actual_fresh_suites':lanes,'actual_transfer':load(actual/'native-result.json'),'router':load(actual/'router-result.json'),'tracker':load(actual/'tracker-result.json'),'namespace_fence':load(actual/'fence-after.json'),'private_cleanup':load(actual/'private-cleanup.json'),'shipping_approved':False,'default_activation':False,'dist_unchanged':True,'original_live_profile_touched':False,'whole_plan_finished':False,'components_remain_in_progress':True,'stop_after_section':True}
write(E/'verification.json',verification)
index={str(f.relative_to(E)):{'sha256':sha(f),'bytes':f.stat().st_size} for f in sorted(E.rglob('*')) if f.is_file()};write(E/'artifacts.json',index);print(json.dumps({'status':verification['status'],'files':len(index),'bytes':sum(x['bytes'] for x in index.values()),'directory':str(E)}))
