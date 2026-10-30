"""Root-reviewed sequential qualification; exits at the first failed gate."""
import hashlib,json,pathlib,subprocess,sys,time
T=pathlib.Path(__file__).resolve().parent;C=T/'provider-candidate-r2';H=T/'provider-recipe/sdk-r2-configured-continuation';N=T/'native-followup-20261030/amendments/live-executor-sdk';A=T/'final-transfer-live-executor';S='9f46cf05d133624e48c6d60d45d2c503a6141ff9';OUT=T/'finish-gates-r3';OUT.mkdir(exist_ok=False)
sha=lambda p:hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
load=lambda p:json.loads(pathlib.Path(p).read_bytes())
def run(name,args):
 print(name+' START',flush=True)
 with (OUT/(name+'.log')).open('wb') as log:result=subprocess.run(list(map(str,args)),stdout=log,stderr=subprocess.STDOUT)
 if result.returncode:raise RuntimeError(name+' failed; see '+str(OUT/(name+'.log')))
 print(name+' PASS',flush=True)
try:
 deadline=time.monotonic()+7200;ready=C/'runs/r3/result.json'
 while not ready.exists():
  if time.monotonic()>=deadline:raise TimeoutError('SDK result not available in7200s')
  time.sleep(2)
 assert load(ready)['status']=='ACTUAL_PRIVATE_I2P_PROVIDER_DIRECT_CONTROLS_PASS_NOT_APPROVED',load(ready)
 run('sdk-record',['python3','-B',H/'provider.py','record','--output',C,'--attempt','r3'])
 run('sdk-review',['python3','-B',H/'review-artifact.py'])
 approval=C/'root-private-owner-approval.json';digest=sha(approval)
 run('native',['/usr/bin/make','--no-print-directory','--jobserver-style=fifo','-j3','-f',N/'jobs.mk','all','ATTEMPT=r1','SOURCE_COMMIT='+S,'ADOPTION='+str(T/'implementation/amendments/outgoing-unspecified-port/provider-test-lifetime-pin-adoption.json'),'PROVIDER_RECORD='+str(C/'source-build-record.json'),'PROVIDER_APPROVAL='+str(approval),'PROVIDER_APPROVAL_SHA256='+digest,'PROVIDER_RUNTIME_MANIFEST='+str(C/'approval-proposal/private-i2p-owner-runtime.json'),'PROVIDER_REVIEW='+str(C/'root-artifact-review/review.json')])
 for lane in ('clang','gcc','san','default'):
  value=load(N/('runs/'+lane+'-r1/result.json'));assert value['source_commit']==S and value['status']==('PASS_DEFAULT_I2P_ATTACHMENT_CONTROLS' if lane=='default' else 'PASS_PRIVATE_I2P_OWNER_AFFECTED_COHORT')
 run('transfer',['python3','-B',A/'run.py','--native-result',N/'runs/gcc-r1/result.json','--adoption',T/'implementation/amendments/outgoing-unspecified-port/provider-test-lifetime-pin-adoption.json','--source-commit',S,'--expected-native-status','PASS_PRIVATE_I2P_OWNER_AFFECTED_COHORT','--provider-record',C/'source-build-record.json','--provider-approval',approval,'--provider-approval-sha256',digest,'--output',A/'runs/r1','--acknowledge-warmed-local-cache-only'])
 assert load(A/'runs/r1/result.json')['status']=='PASS_REAL_ISOLATED_ROUTER_PROVIDER_TRANSFER'
 result={'status':'PASS_SDK_NATIVE_AND_REAL_TRANSFER_GATES','source_commit':S,'provider_record_sha256':sha(C/'source-build-record.json'),'approval_sha256':digest,'actual_transfer_result_sha256':sha(A/'runs/r1/result.json'),'default_shipping_changed':False,'stop_before_next_section':True}
except BaseException as error:result={'status':'FAIL','failure':repr(error)}
(OUT/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result),flush=True);sys.exit(0 if result['status']=='PASS_SDK_NATIVE_AND_REAL_TRANSFER_GATES' else 1)
