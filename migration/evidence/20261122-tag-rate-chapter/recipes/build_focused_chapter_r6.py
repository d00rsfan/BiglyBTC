"""Complete application,real router probes and affected rate/source/UI tests."""
from pathlib import Path
import hashlib,json,subprocess,time,os,signal
D=Path(__file__).resolve().parent;R=D.parents[1];N=R.parent/'BiglyBTC/preview/native';B=D/'native-final-gcc'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def inventory():return {str(p.relative_to(N)):sha(p) for p in sorted(N.rglob('*')) if p.is_file() and '__pycache__' not in p.parts}
def available():return int(next(x.split()[1] for x in Path('/proc/meminfo').read_text().splitlines() if x.startswith('MemAvailable:')))
assert (D/'focused-build-transition.json').exists(),'Retain/join the successful original engine/Host build first'
commands=[]
configure=json.loads((D/'native-final-configure.json').read_text())[0]['argv']
assert (B/"CMakeCache.txt").is_file(),"Reuse the already verified unchanged configuration"
source=inventory();sdk=Path(json.loads((D.parent/'tag-rate-original-policy-sdk-20261121/sdk-final-r6-artifact.json').read_text())['artifact'])
probes=['biglybtc-private-tag-transfer-probe','biglybtc-tag-rate-coexist-host-probe','biglybtc-tag-rate-mixed-host-probe','biglybtc-tag-rate-pending-host-probe']
targets=json.loads((D/'focused-target-candidates.json').read_text())
targets=sorted(set(targets+['biglybtc-private-owner-refresh-tests','biglybtc-private-rate-membership-tests','biglybtc-private-tag-host-tests','biglybtc-profile-network-policy-tests','biglybtc-profile-tag-readmission-tests']))
(D/'focused-qualified-targets.json').write_text(json.dumps(targets,indent=2)+'\n')
for phase,wanted,initial in [('app-and-router-probes',['BiglyBTC','BiglyBTC-headless',*probes],1),('affected-tests',targets,2)]:
 jobs=initial
 while True:
  if available()<5*1024*1024:raise RuntimeError('Require at least5GiB available before compiler start')
  argv=['nice','-n','10','cmake','--build',str(B),'--parallel',str(jobs),'--target',*wanted]
  item=dict(phase=phase,argv=argv,start_available_kib=available(),memory_fallback=False);commands.append(item)
  process=subprocess.Popen(argv,start_new_session=True)
  while process.poll() is None:
   time.sleep(.5)
   if jobs>1 and available()<5*1024*1024:
    item['memory_fallback']=True;os.killpg(process.pid,signal.SIGINT);break
  item['exit_code']=process.wait();item['end_available_kib']=available()
  (D/'focused-build-memory-receipt-r6.json').write_text(json.dumps(commands,indent=2)+'\n')
  if item['memory_fallback']:jobs=1;continue
  if item['exit_code']:raise SystemExit(item['exit_code'])
  break
 assert inventory()==source,'Native inputs changed during qualified build'
 if phase=='app-and-router-probes':
  images={name:dict(path=str(p),bytes=p.stat().st_size,sha256=sha(p)) for name,p in [('BiglyBTC',B/'app/BiglyBTC'),('BiglyBTC-headless',B/'app/BiglyBTC-headless'),*[(name,B/'tests'/name) for name in probes]]}
  receipt=dict(status='PASS',scope='complete application and router probe build;runtime/unit qualification recorded separately',sdk_sha256=sha(sdk),native_source_sha256=source,images=images,commands=commands.copy(),unit_qualification_pending=True)
  (D/'final-build-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
  print('APP_AND_FOUR_ROUTER_PROBES_READY',flush=True)
print('COMPLETE_APPLICATION_AND_AFFECTED_TESTS_BUILT',len(targets),flush=True)
