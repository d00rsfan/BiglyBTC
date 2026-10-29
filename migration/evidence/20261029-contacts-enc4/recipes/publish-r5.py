"""Root-only publication after actual contacts/Enc4 qualification and promotion."""
import hashlib,json,pathlib,shutil,subprocess
R=pathlib.Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';H=pathlib.Path(__file__).resolve().parent;E=pathlib.Path('migration/evidence/20261029-contacts-enc4')
def read(p):return json.loads(pathlib.Path(p).read_text())
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def git(*args,**kw):return subprocess.run(['git','-C',str(N),*args],check=True,**kw)
def append(name,text):
 p=R/name;p.write_text(p.read_text().rstrip()+'\n\n'+text.strip()+'\n')
def main():
 assert not (H/'publish.json').exists();promotion=read(H/'promotion.json');source=promotion['source_commit'];v=read(R/E/'verification.json')
 assert v['status']=='PASS_CONTACTS_ENC4' and v['source_commit']==source and v['promotion']==promotion
 assert subprocess.check_output(['git','-C',str(N),'rev-parse','HEAD'],text=True).strip()==source
 assert subprocess.check_output(['git','-C',str(N),'branch','--show-current'],text=True).strip()=='main'
 assert not subprocess.check_output(['git','-C',str(N),'diff','--cached','--name-only'],text=True).strip()
 assert set(subprocess.check_output(['git','-C',str(N),'diff','--name-only'],text=True).splitlines())<={'migration/upstreams.lock.json'}
 for name,pin in v['artifacts_sha256'].items():assert sha(R/E/name)==pin
 assert (R/E/'verification.json').read_bytes()==(N/E/'verification.json').read_bytes()
 assert sha(N/'dist/BUILD_MANIFEST.json')==promotion['manifest_sha256']
 dist=read(N/'dist/BUILD_MANIFEST.json');assert len(dist['files_sha256'])==promotion['leaf_count']
 assert {str(p.relative_to(N/'dist')):sha(p) for p in (N/'dist').rglob('*') if p.is_file() and p.name!='BUILD_MANIFEST.json'}==dist['files_sha256']
 assert promotion['previous_dist_removed'] and promotion['atomic_exchange'] and promotion['test_support_removed'] and promotion['both_registry_preflights_passed']
 assert promotion['copied_test_images_removed']==v['private_suites']
 assert set(v['registry_validation'])=={R.name,N.name}
 for label,row in v['registry_validation'].items():assert row['exit']==0 and sha(R/E/('registry-validation-'+label+'.log'))==row['log_sha256']
 lock=(R/'migration/upstreams.lock.json').read_bytes();assert lock==(N/'migration/upstreams.lock.json').read_bytes()==(R/E/'proposed-upstreams.lock.json').read_bytes()
 registry=json.loads(lock);assert (len(registry['upstreams']),len(registry['components']),len(registry['external_dependencies']))==(20,108,22)
 assert all(c['state']=='in_progress' for c in registry['components'] if c['id'] in ('transport.azureus_volatile_database','services.i2cp_initial_session'))
 lanes='/'.join(str(v['lanes'][lane]['actual_suites']) for lane in ('gcc','clang','san'));private=v['private_suites'];review='[Review](evidence/20261029-contacts-enc4/REVIEW.md).'
 assert (v['offline_cases_per_reference_per_direction'],v['offline_java_to_native_frames_per_reference'],v['offline_native_to_java_frames_per_reference'],v['offline_signature_negative_per_reference'])==(4,8,4,1)
 summary=f'Source `{source}` qualifies opt-in owned Azureus timed maintenance/contact persistence and explicit Enc4/X25519 LeaseSet2 keys. Actual GCC/Clang/leak-enabled ASan+UBSan suites {lanes}, private {private}, both-JAR original objects (4 cases per reference and direction), and real isolated-router Enc4 tracker exchange pass. '+review
 gaps='The explicit AllowPeer-to-normal-routing contact attachment seam, public/default/Host routing and I2P backend activation, signed-negative network paths, multitype lease keys, remote ECIES ratchet/multihop discovery, native Streaming window/performance and the broader P05–P13 remain open.'
 (R/'migration/STATE.md').write_text('# Migration state\n\nRead [ACTIVE.md](ACTIVE.md) for authoritative continuation. The entire migration plan remains authorised.\n\n'+summary+f'\n\nOne current dist: manifest `{promotion["manifest_sha256"]}`, {promotion["leaf_count"]} leaves. Both proposed registries passed before atomic exchange; predecessor and {private} copied test images/QtTest support were removed. Failed compile/maintenance attempts and the separate Enc4 supplement remain historical evidence without cohort PASS credit.\n\n'+gaps+'\n')
 tasks={
 'migration/tasks/P06-owned-maintenance-contacts.md':'# Owned Azureus maintenance and contacts\n\nState: in_progress; bounded opt-in slice qualified.\n\n'+summary+'\n\nOwnedLoopback uses genuine 10-second timer work and sixth-tick refresh, with profile-I/O jobs and one-shot current packet permits. The five-minute idle boundary has a pure policy control; it is not a real five-minute operational wait claim. Original raw contacts persistence uses held-profile authority, compatible contact framing, bounded selection and restart. Current adapter supports nonnegative network paths; signed-negative original pathnames remain unsupported. Cancellation, stale work, initial/reply preflights, source changes and shutdown retain finite authority. Kernel-stall and complete resource/performance parity are not claimed. Original cb8c6cb9bb97916cbc7e80338a017200ffc3e71f remains the source baseline.\n\n'+gaps+'\n',
 'migration/tasks/P07-i2cp-enc4.md':'# Explicit Enc4 LeaseSet2 keys\n\nState: in_progress; bounded original-format and local-router slice qualified.\n\n'+summary+'\n\nGeneric absent encryption policy stays Enc0. Explicit `4`/`ECIES_X25519` selects a genuine independent 32-byte pair; it does not alter Destination, signing identity, keyfile or hash. Renewal reuses the owned pair and acknowledged merged configuration seals its type. Unsupported multiple/other types refuse without alternate-key fallback. OpenSSL supplies the existing raw X25519 primitive and secret erasure; no native ratchet implementation or new dependency is claimed.\n\nActual official and modified Java objects verify four DSA0/Ed7 one/sixteen-lease cases per reference and direction: eight Java-to-native frames, four native-to-Java frames and changed-signature rejection. The exact 14 official source objects are retained; historical lazy-missing metadata is preserved. A real fenced router accepts current signed one-key Enc4 LS2 for both unchanged Destinations and carries signed2 connect/claimed3 scrape/RAW18 counts3/4/5. Warm signed-cache/zero-hop is an explicit fixture precondition. This qualifies neither anonymous remote tunnels nor default plugin/Host activation. Official9134f808337b401e8e53c73734c81fab04280c9d and modified b0866aa843a55d9290755c7a5cc3091cda765374544914afb4d3557ae99994b5 remain distinct provenances.\n'}
 assert all(not (R/name).exists() for name in tasks)
 for name,text in tasks.items():(R/name).write_text(text)
 lines=(R/'migration/FEATURE_MATRIX.md').read_text().splitlines();seen=set()
 for i,line in enumerate(lines):
  if line.startswith('| F09.db_mapping |'):
   lines[i]=f'| F09.db_mapping | Original mapping and owned volatile runtime | [Runtime](tasks/P06-AZUREUS-VOLATILE-DATABASE.md), [maintenance](tasks/P06-owned-maintenance-contacts.md) | In progress; `{source}` qualifies opt-in genuine timed maintenance and original nonnegative-network contacts persistence/restart. Five-minute idle is pure policy coverage; public/Host, signed-negative paths, republish and full performance remain open. {review} |';seen.add('db')
  if line.startswith('| F06.i2cp_initial_session |'):
   lines[i]=f'| F06.i2cp_initial_session | Signed I2CP, datagrams, tracker, Streaming and explicit Enc4 | [Session](tasks/P07-02b-i2cp-initial-session.md), [Enc4](tasks/P07-i2cp-enc4.md) | In progress; `{source}` qualifies independent Enc4 LS2 keys with unchanged Destination, both-reference objects and actual warm-cache isolated-router tracker counts/retirement. Prior window1 Streaming remains qualified; full backend/window/tunnel/default/performance parity remains open. {review} |';seen.add('i2cp')
 assert seen=={'db','i2cp'};(R/'migration/FEATURE_MATRIX.md').write_text('\n'.join(lines)+'\n')
 for name in ('MIGRATION_PLAN.md','migration/tasks/P06-AZUREUS-VOLATILE-DATABASE.md','migration/tasks/P07-02b-i2cp-initial-session.md'):
  append(name,'## 2026-10-29 owned maintenance, contacts and Enc4\n\n'+summary+'\n\nOne checked dist replaces its predecessor after both registry validations. Existing 20 upstreams, 108 components, 22 dependencies, states and baseline/null integration fields are unchanged. '+gaps)
 p=R/'migration/ACTIVE.md';text=p.read_text();i=text.find('\n## ');i=len(text) if i<0 else i+1
 # Insert before the first section; preserve every prior heading/history byte.
 text=text[:i].rstrip()+'\n\n'+f'''## Latest accepted milestone — read before older notes

K contacts/maintenance + Enc4 source `{source}` is qualified and promoted once. Actual GCC/Clang/SAN suites {lanes}, private {private}, both-reference Enc4 objects4/reference/direction and actual fenced Enc4 router tracker pass. Dist `{promotion['manifest_sha256']}` has {promotion['leaf_count']} leaves; old dist/test copies/QtTest were removed after both registry preflights. All failed attempts remain separate. K/publish.json records the actual metadata commit/push. Continue the whole plan; native Streaming window and broader public/backend/performance work remain open. Historical notes below are not current running jobs.

'''+text[i:];p.write_text(text)
 names=['MIGRATION_PLAN.md','migration/ACTIVE.md','migration/STATE.md','migration/FEATURE_MATRIX.md','migration/upstreams.lock.json',*tasks,'migration/tasks/P06-AZUREUS-VOLATILE-DATABASE.md','migration/tasks/P07-02b-i2cp-initial-session.md']
 for name in names:shutil.copyfile(R/name,N/name)
 git('add','--',*names,str(E))
 # Retain original byte-exact logs, XML, patches and synthetic wire vectors.
 excludes=[':(exclude)**/*.'+x for x in ('log','xml','patch','txt','dest')]+[':(exclude)**/original-source/**',':(exclude)'+str(E)+'/offline/**/vectors/**']
 with (H/'diff-check.log').open('wb') as f:git('diff','--cached','--check','--','.',*excludes,stdout=f,stderr=subprocess.STDOUT)
 git('-c','user.name=Codex','-c','user.email=codex@localhost','-c','commit.gpgsign=false','commit','-q','-m','Qualify owned maintenance contacts and Enc4 lease keys')
 commit=subprocess.check_output(['git','-C',str(N),'rev-parse','HEAD'],text=True).strip()
 with (H/'push.log').open('wb') as f:git('push','origin','main',stdout=f,stderr=subprocess.STDOUT)
 assert not subprocess.check_output(['git','-C',str(N),'status','--porcelain'],text=True).strip()
 (H/'publish.json').write_text(json.dumps({'commit':commit,'branch':'main','pushed':True,'source_commit':source},indent=2)+'\n');print('COMMITTED_PUSHED_CLEAN',commit)
if __name__=='__main__':main()
