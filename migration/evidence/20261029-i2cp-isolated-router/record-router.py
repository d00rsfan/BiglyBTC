import hashlib,json,os,pathlib,shutil,subprocess
R=pathlib.Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';H=pathlib.Path(__file__).resolve().parent;T=R/'build/i2cp-isolated-router-20261029';U=T/'runs/r1';B=H/'router-build-r1';E=pathlib.Path('migration/evidence/20261029-i2cp-isolated-router');SOURCE='f9c72f14be1494b6d75b38288f9eff256a1a878b'
def load(p):return json.loads(pathlib.Path(p).read_text())
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def write(p,d):pathlib.Path(p).write_text(json.dumps(d,indent=2)+'\n')
def git(*a,**kw):return subprocess.run(['git','-C',str(N),*a],check=True,**kw)
assert not (R/E).exists() and not (N/E).exists() and not (H/'router-publish.json').exists()
assert subprocess.check_output(['git','-C',str(N),'rev-parse','HEAD'],text=True).strip()=='9aee392e40fa50ea966bfa151d32ababb1dd71e9';assert not subprocess.check_output(['git','-C',str(N),'status','--porcelain'],text=True).strip()
assert sha(T/'manifest.json')=='ea3835eddf67a2a92abe2085723ececd9647fa8a73e42d020505d598c0530cb7';m=load(T/'manifest.json')
for name,pin in m['artifacts'].items():assert sha(T/name)==pin
assert load(U/'result.json')['status']=='PASS_WARMED_LOCAL_LOOKUP_AND_REAL_ROUTER_TRACKER' and load(U/'namespace-exit.json')['exit_code']==0
commands=load(U/'commands.json');assert [r['label'] for r in commands]==['router','tracker','native'] and all(r['exit_code']==0 for r in commands)
assert load(U/'router-result.json')['remaining_clients']==0 and load(U/'router-result.json')['public_transports']==0
native=load(U/'native-result.json');assert (native['seeds'],native['downloaded'],native['peers'])==(3,4,5) and not native['general_receiver_enabled']
for name in ('fence-before.json','fence-after.json'):assert load(U/name)['status']=='ENFORCED_LOOPBACK_ONLY'
inputs=load(U/'inputs.json');assert inputs['source_commit']==SOURCE
for path,pin in inputs['archives'].items():assert sha(path)==pin
for path,pin in inputs['native_source'].items():assert sha(N/path)==pin
probe=pathlib.Path(load(B/'result.json')['probe']);assert sha(probe)==inputs['probe_sha256'] and not (U/'native.keys').exists()
assert all(r['exit']==0 for r in load(B/'commands.json'))
oldlock=(R/'migration/upstreams.lock.json').read_bytes();assert (N/'migration/upstreams.lock.json').read_bytes()==oldlock;old_dist=sha(N/'dist/BUILD_MANIFEST.json')
out=R/E;out.mkdir(parents=True);artifacts={}
def copy(p,name):
 target=out/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,target);artifacts[name]=sha(target)
for name in m['artifacts']:copy(T/name,'subject/'+name)
copy(T/'manifest.json','subject/manifest.json')
for name in ('namespace-command.json','fence-after.json','namespace-exit.json','native-lease.json','router-result.json','cache-precondition.json','inputs.json','tracker-ready.json','native-retired.json','result.json','fence-before.json','tracker-result.json','router-ready.json','commands.json','native-result.json'):copy(U/name,'run/'+name)
for name in ('configure.log','build.log','javac.log','commands.json','result.json'):copy(B/name,'build/'+name)
copy(B/'probe/link-inputs.json','build/link-inputs.json');copy(H/'build-router-probe.py','build/runner.py');copy(H/'record-router.py','record-router.py');copy(H/'router-output-path-refusal.log','history/output-path-refusal.log')
for name in ('command.json','command.log','fence.json'):copy(H/'router-fence-preflight'/name,'preflight/'+name)
(out/'REVIEW.md').write_text(f'''# Actual isolated-router tracker exchange

Qualified native source `{SOURCE}` and exact modified original JAR b0866aa843a55d9290755c7a5cc3091cda765374544914afb4d3557ae99994b5. The actual Java26 Router and separate external-I2CP Java tracker exchanged Hash38/39, signed2 BEP15 connect, claimed3 scrape and two RAW18 replies with the current native channel. Native observed seeds3/completed4/peers5 in {native['elapsed_ms']}ms; its general datagram receiver remained disabled. All three spawned subjects exited0, both clients retired and router shut down. Existing product ELFs/source and eight archive inputs remained unchanged; only the standalone probe and release17 Java fixture were compiled serially, using the actual passed GCC inputs. No prior core/provider/whole HTTP tests were replayed.

The tested scope is warmed same-router local forwarding. Both actual registered zero-hop clients supplied accepted, current, signature-verified LS2s; the fixture copied only the real tracker LS2 into the native client's netDB through public store. This explicit precondition is recorded and does not fabricate an I2CP response. Discovery, remote tunnels, encryption in transit, Enc4/default crypto, Streaming, Host/backend/default activation, whole modified-core source equivalence and full P07 parity remain open.

Before any subject and after completion, bwrap enforced a fresh network/PID/user namespace, only loopback/routes, zero capabilities and NoNewPrivs, with unreachable documentation addresses. Host home/run/tmp/profiles/sockets were absent. Public transports/reseed/NTP/UPnP were disabled; the actual router reported zero public transports/peers. Only owned Popen children were signalled. The first root launch stopped at the output-directory guard before any subject; corrected CLI only, no source/test change. Full runtime logs stay local; public evidence contains source-bound commands and reviewed receipts. Synthetic private profiles/keys and compiled probe/classes are removed after recording. Existing dist, states, baseline fields and22dependencies remain unchanged.
''')
artifacts['REVIEW.md']=sha(out/'REVIEW.md');lock=json.loads(oldlock);before=json.loads(oldlock);component=next(c for c in lock['components'] if c['id']=='services.i2cp_initial_session')
component['review_records'].append(str(E/'REVIEW.md'));component['evidence'].append(str(E/'verification.json'))
for b,a in zip(before['components'],lock['components'],strict=True):
 if b['id']!=component['id']:assert b==a
 else:assert {k:v for k,v in b.items() if k not in ('review_records','evidence')}=={k:v for k,v in a.items() if k not in ('review_records','evidence')}
verification={'status':'PASS_ACTUAL_ISOLATED_ROUTER_TRACKER','source_commit':SOURCE,'inputs':inputs,'subjects':commands,'native':native,'scope':'warmed local lookup and actual same-router forwarding only','public_or_default_activation':False,'native_rebuilt':False,'provider_rebuilt':False,'previous_tests_replayed':False,'dist_manifest_sha256':old_dist,'artifacts_sha256':artifacts}
write(out/'verification.json',verification);shutil.copytree(out,N/E);proposed=H/'router-proposed-upstreams.lock.json';write(proposed,lock);validation={}
code='import json,pathlib,runpy,sys;runpy.run_path(sys.argv[1])["validate"](json.loads(pathlib.Path(sys.argv[2]).read_bytes()))'
for root in (R,N):
 log=out/('registry-'+root.name+'.log')
 with log.open('wb') as stream:p=subprocess.run(['python3','-B','-c',code,str(root/'migration/tools/validate.py'),str(proposed)],cwd=root,env=dict(os.environ,GIT_NO_LAZY_FETCH='1',GIT_OPTIONAL_LOCKS='0'),stdout=stream,stderr=subprocess.STDOUT)
 assert p.returncode==0,str(log);artifacts[log.name]=sha(log);shutil.copyfile(log,N/E/log.name);validation[root.name]={'exit':0,'log_sha256':sha(log)}
# Every target below belongs to this fresh completed acceptance; no user profile.
removed=[]
for target in (U/'router',U/'peer-data',U/'home',B/'probe',B/'classes'):
 assert not target.is_symlink() and target.parent.resolve() in (U.resolve(),B.resolve())
 if target.exists():shutil.rmtree(target);removed.append(str(target))
verification.update(registry_validation=validation,cleanup={'owned_targets_removed':removed,'native_private_keys_absent':True,'public_logs_or_sources_only':True});write(out/'verification.json',verification);shutil.copyfile(out/'verification.json',N/E/'verification.json')
for root in (R,N):
 target=root/'migration/upstreams.lock.json';assert target.read_bytes()==oldlock;pending=target.with_suffix('.router-pending');assert not pending.exists();pending.write_bytes(proposed.read_bytes());os.replace(pending,target)
paragraph=f'Actual isolated-router acceptance on native `{SOURCE}` passes warmed Hash38/39 and signed2connect/claimed3scrape/RAW18 through a real Java26 router and external tracker. All3subjects exit0, both clients retire, fences hold and dist remains `{old_dist}`. Warmed actual signed-LS2 cache is an explicit fixture prerequisite; discovery/tunnels/Streaming/default activation remain open. [Review](evidence/20261029-i2cp-isolated-router/REVIEW.md).'
for name in ('MIGRATION_PLAN.md','migration/STATE.md','migration/tasks/P07-02b-i2cp-initial-session.md'):
 p=R/name;p.write_text(p.read_text().rstrip()+'\n\n## 2026-10-29 actual isolated-router acceptance\n\n'+paragraph+'\n');shutil.copyfile(p,N/name)
p=R/'migration/FEATURE_MATRIX.md';s=p.read_text().replace('Real-router/Streaming/backend/default and complete plugin activation remain open.','Warmed same-router external-I2CP tracker acceptance also passes with actual signed-LS2 cache prerequisite; discovery/tunnels/Streaming/backend/default and complete plugin activation remain open. [Router review](evidence/20261029-i2cp-isolated-router/REVIEW.md).');p.write_text(s);shutil.copyfile(p,N/'migration/FEATURE_MATRIX.md')
p=R/'migration/ACTIVE.md';s=p.read_text();start=s.index('Next root task: actual isolated-router');end=s.index('Existingagents SOURCE ONLY:',start);s=s[:start]+f'''Actual isolated-router acceptance COMPLETE: build/i2cp-isolated-router-20261029/runs/r1 PASS, outer81351 JOINED0, actual3subjects0/clientsretired/fencesbefore+after. Source{SOURCE}; Java26modifiedb086; nativecounts3/4/5,2221ms; generalreceiverfalse. Explicit actual signedLS2 warmcache fixture, no discovery/tunnels/default/public/Streaming claim. Evidence migration/evidence/20261029-i2cp-isolated-router; bothregistryvalidationspassed; ownedprofiles/keys/probe/classesremoved, no dist/source/providerchange. Msourcepublished9aee392e40fa50ea966bfa151d32ababb1dd71e9; originalcheckpoint11946JOINED0 e9caf4f47f13c09e575cc7a3932126fe7bdf35bd. NEXT checkpoint expected-parente9caf4f47f13c09e575cc7a3932126fe7bdf35bd until routercheckpointcreated. Next root: final routing child/review +Streaming/Java peer capsules pending; use sharedGNUmake/Ninja pool3 in build/build-scheduling-20261029/PLAN.txt for nextactualbatch.\n\n'''+s[end:];p.write_text(s);shutil.copyfile(p,N/'migration/ACTIVE.md')
names=['MIGRATION_PLAN.md','migration/STATE.md','migration/FEATURE_MATRIX.md','migration/ACTIVE.md','migration/tasks/P07-02b-i2cp-initial-session.md','migration/upstreams.lock.json',str(E)];git('add','--',*names)
with (H/'router-diff-check.log').open('wb') as stream:git('diff','--cached','--check','--','.',':(exclude)**/*.log',':(exclude)**/*.xml',':(exclude)**/*.patch',':(exclude)**/commands.txt',':(exclude)**/original-source/**',stdout=stream,stderr=subprocess.STDOUT)
assert sha(N/'dist/BUILD_MANIFEST.json')==old_dist
git('-c','user.name=Codex','-c','user.email=codex@localhost','-c','commit.gpgsign=false','commit','-q','-m','Verify tracker exchange through isolated original I2P router')
commit=subprocess.check_output(['git','-C',str(N),'rev-parse','HEAD'],text=True).strip()
with (H/'router-push.log').open('wb') as stream:git('push','origin','main',stdout=stream,stderr=subprocess.STDOUT)
assert not subprocess.check_output(['git','-C',str(N),'status','--porcelain'],text=True).strip();write(H/'router-publish.json',{'commit':commit,'pushed':True,'source_commit':SOURCE});print('ROUTER_EVIDENCE_PUSHED',commit)
