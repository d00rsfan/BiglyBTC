from pathlib import Path
import hashlib,json,subprocess,shutil
R=Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';T=R/'build/i2p-multi-20261030';E=R/'migration/evidence/20261030-i2p-multi-torrent'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def load(p):return json.loads(p.read_text())
def write(p,v):p.write_text(json.dumps(v,indent=2)+'\n')
def git(root,*args):return subprocess.check_output(['git',*args],cwd=root,text=True).strip()
before=load(T/'preservation-before.json');head=git(N,'rev-parse','HEAD');v=load(E/'verification.json');assert v['status']=='PASS_BOUNDED_SHARED_SESSION_MULTI_I2P_CHAPTER';code=v['source_commit'];assert head==code
assert git(R,'rev-parse','HEAD')==before['original_head'];assert sha(R/'.git/index')==before['original_index_sha256'];assert git(R,'rev-parse','refs/heads/biglybtc/native-20261017')==before['review_ref']
for folder,expected in [('dist','aad928753cb8f2dac8f99245298d987cfe2c4bee9e7bdebf5b8d7645ed6d559b'),('dist-i2p',load(T/'bundle.json')['manifest_sha256'])]:
 p=N/folder/'BUILD_MANIFEST.json';assert sha(p)==expected
 assert all(sha(N/folder/name)==digest for name,digest in load(p)['files_sha256'].items())
preservation={'status':'PASS','original_head':before['original_head'],'original_index_sha256':before['original_index_sha256'],'review_ref_before_final_checkpoint':before['review_ref'],'default_dist_manifest_sha256':sha(N/'dist/BUILD_MANIFEST.json'),'private_dist_manifest_sha256':sha(N/'dist-i2p/BUILD_MANIFEST.json'),'live_profile_used':False,'original_process_interrupted':False};write(T/'final-preservation.json',preservation)
for name in ['final-preservation.json','hosted-ci-polls.jsonl']:
 shutil.copy2(T/name,E/'recipes'/name)
manifest={'status':'SEALED_PUBLIC_EVIDENCE','qualified_runtime_source':code,'source_tree':git(N,'rev-parse',code+':preview/native'),'files_sha256':{p.relative_to(E).as_posix():sha(p) for p in sorted(E.rglob('*')) if p.is_file() and p!=E/'manifest.json'}};write(E/'manifest.json',manifest)
shutil.copytree(E,N/E.relative_to(R),dirs_exist_ok=True)
assert (R/'migration/upstreams.lock.json').read_bytes()==(N/'migration/upstreams.lock.json').read_bytes()
print(json.dumps({'status':'READY_TO_COMMIT_FINAL_PUBLICATION','source':code,'evidence_files':len(manifest['files_sha256']),'manifest_sha256':sha(E/'manifest.json')}))
