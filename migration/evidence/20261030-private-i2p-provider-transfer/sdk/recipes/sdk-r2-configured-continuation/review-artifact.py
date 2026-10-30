"""Root independent measurement and explicitly scoped private-owner approval."""
import hashlib,json,os,pathlib,re,subprocess,xml.etree.ElementTree as ET
H=pathlib.Path(__file__).resolve().parent;T=H.parents[1];C=T/'provider-candidate-r2'
sha=lambda p:hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
load=lambda p:json.loads(pathlib.Path(p).read_bytes())
def write(p,d):p.write_text(json.dumps(d,indent=2)+'\n')
def tree(p):return {str(f.relative_to(p)):sha(f) for f in p.rglob('*') if f.is_file()}
r=load(C/'source-build-record.json');runtime=C/'approval-proposal/private-i2p-owner-runtime.json';v=load(runtime)
assert r['source_capsule_sha256']=='9d264115b5964f2ed542d193697b77263f974f0d4c3f929e829bd1c4c1398671'
assert r['status']=='BUILT_DIRECTLY_TESTED_NOT_APPROVED' and r['native_runtime_approved'] is False and r['shipping_approved'] is False
assert r['actual_controls']['actual_ctest_suites']==9 and r['actual_controls']['status']=='ACTUAL_PRIVATE_I2P_PROVIDER_DIRECT_CONTROLS_PASS_NOT_APPROVED'
assert v['source_build_record_sha256']==sha(C/'source-build-record.json') and v['native_runtime_approved'] is False
assert tree(C/'source')==r['source_inputs_sha256'] and len(r['source_inputs_sha256'])==1087
assert tree(C/'install')==r['installed_sha256']
for name,pin in r['evidence_sha256'].items():assert sha(C/name)==pin,name
for name,pin in r['reviewed_inputs_sha256'].items():assert sha(name)==pin,name
headers=tree(C/'install/include/libtorrent');headers={'libtorrent/'+k:v for k,v in headers.items()};assert headers==r['headers_sha256']
sdk_hash=hashlib.sha256(json.dumps(headers,sort_keys=True,separators=(',',':')).encode()).hexdigest();assert sdk_hash==r['sdk_tree_sha256']
artifact=pathlib.Path(r['artifact']);assert sha(artifact)==r['artifact_sha256']==v['artifact_sha256'] and artifact.stat().st_size==r['artifact_bytes']
for name,pin in load(C/'dependencies.json').items():assert sha(name)==pin,name
assert sha(r['compiler']['resolved'])==r['compiler']['sha256']
run=C/'runs/r3';commands=load(run/'commands.json');assert len(commands)==11 and all(x['exit']==0 and sha(C/x['log'])==x['log_sha256'] for x in commands)
cases=[]
for name in ('provider-tests','wire-controls','mainline-tests','socks4a-tests'):
 rows=list(ET.parse(run/(name+'.xml')).getroot().iter('testcase'));assert all(x.get('status')=='run' and not any(x.find(tag) is not None for tag in ('failure','error','skipped')) for x in rows);cases+=rows
assert len(cases)==9 and len({x.get("name") for x in cases})==9
images=load(run/'images-before.json');assert images==load(run/'images-after.json') and all(sha(C/k)==v for k,v in images.items())
objects=load(run/'hidden-objects-before.json');assert objects==load(run/'hidden-objects-after.json') and all(sha(C/k)==v for k,v in objects.items())
exports=subprocess.check_output(['nm','-D','--defined-only',str(artifact)],text=True);names={line.split()[-1] for line in exports.splitlines()};assert len(set(r['required_exports']))==31 and all(name in names for name in r['required_exports'])
out=C/'root-artifact-review';out.mkdir(exist_ok=False);(out/'exports.txt').write_text(exports)
env=dict(os.environ);env['LD_LIBRARY_PATH']=str(C/'install/lib');maps={}
for rel in load(C/'control-loader.json'):
 text=subprocess.check_output(['ldd',str(C/rel)],env=env,text=True);assert 'not found' not in text;found=re.findall(r'libtorrent-rasterbar\.so\.2\.0 => (/\S+) ',text);assert len(found)==1 and sha(found[0])==r['artifact_sha256'];maps[rel]=text
write(out/'loader.json',maps)
review={'status':'MEASURED_PRIVATE_OWNER_ARTIFACT_REVIEW_PASS','source_build_record_sha256':sha(C/'source-build-record.json'),'source_capsule_sha256':r['source_capsule_sha256'],'artifact_sha256':r['artifact_sha256'],'artifact_bytes':r['artifact_bytes'],'sdk_tree_sha256':sdk_hash,'source_leaves':1087,'installed_leaves':len(r['installed_sha256']),'header_leaves':len(headers),'actual_ctest_suites':9,'actual_commands':len(commands),'required_exports':31,'loader_images':len(maps),'same_build_hidden_objects':objects,'deterministic_lifecycle_source_and_full_unit_run':True,'reviewer_build_or_subject_execution':False,'reviewer_artifact_operations':['rehash closed source/SDK/dependencies/tools/tested images','parse fresh XML and command receipts','read actual same-image exports','ldd six qualified executables'],'shipping_approved':False,'default_activation':False}
write(out/'review.json',review)
approval={'schema':'BIGLYBTC_PRIVATE_I2P_OWNER_APPROVAL_V1','source_capsule_sha256':r['source_capsule_sha256'],'artifact_sha256':r['artifact_sha256'],'artifact_bytes':r['artifact_bytes'],'sdk_tree_sha256':sdk_hash,'runtime_manifest_sha256':sha(runtime),'source_build_record_sha256':sha(C/'source-build-record.json'),'review_sha256':sha(out/'review.json'),'headers_sha256':headers,'scope':'Explicit optional private I2P owner validation only','shipping_approved':False,'default_activation':False}
write(C/'root-private-owner-approval.json',approval);print(json.dumps({'status':review['status'],'artifact':r['artifact_sha256'],'approval':sha(C/'root-private-owner-approval.json'),'record':sha(C/'source-build-record.json')}))
