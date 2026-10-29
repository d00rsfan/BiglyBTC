#!/usr/bin/env python3
"""Finite current-source LeaseSet2 object interoperability; no old Java replays."""
import argparse,importlib.util,pathlib,re
R=pathlib.Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';H=pathlib.Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('batch',H/'run.py');b=importlib.util.module_from_spec(spec);spec.loader.exec_module(b);s=b.s
REG=R/'build/i2cp-leaseset2-20261029';JARS={'official':R/'build/i2p-datagram-codec-20261029/amendments/actual-offline-interop/dependencies/i2p-2.13.0.jar','modified':R/'build/i2p-modified-core-20261029/azi2pcore_2.13.0.1.jar'}
PINS={'official':'d76b3c4c4fa7043c2a3553ab3209954e63151dfd1a90f7c020fbc58b33c89896','modified':'b0866aa843a55d9290755c7a5cc3091cda765374544914afb4d3557ae99994b5'}
def main():
 p=argparse.ArgumentParser();p.add_argument('--attempt',required=True);p.add_argument('--lane-attempt',required=True);a=p.parse_args();assert all(re.fullmatch('[A-Za-z0-9_-]+',x) for x in (a.attempt,a.lane_attempt))
 adoption=s.load(H/'root-adoption.json');source=adoption['source_commit'];before=s.source_snapshot(source);gcc=H/'runs'/('gcc-'+a.lane_attempt);d=s.load(gcc/'result.json');assert d['status']=='PASS_NORMAL_RECEIVER_LEASESET_COHORT' and d['lane']=='gcc' and d['source_commit']==source and s.load(gcc/'source-before.json')==before
 images=s.load(gcc/'images-before.json');assert all(s.sha(v['path'])==v['sha256'] for v in images.values());driver=pathlib.Path(images['biglybtc-i2cp-leaseset-tests']['path'])
 m=s.load(REG/'manifest.json');assert s.sha(REG/'manifest.json')=='14281940e754b94519bce36aff31a5ca45b207a489dc149f0bacdef9d4da80a7'
 for name,pin in m['artifacts_sha256'].items():assert s.sha(REG/name)==pin
 for name,path in JARS.items():assert s.sha(path)==PINS[name]
 out=H/'offline-runs'/a.attempt;out.mkdir(parents=True,exist_ok=False);env=s.environment('gcc')
 for key in ('JAVA_TOOL_OPTIONS','_JAVA_OPTIONS','JDK_JAVA_OPTIONS','CLASSPATH','JAVA','JAVAC','ASAN_OPTIONS','LSAN_OPTIONS','UBSAN_OPTIONS','BIGLYBTC_PREVIEW_PROFILE'):env.pop(key,None)
 env['JAVA_TOOL_OPTIONS']='-XX:ActiveProcessorCount=2'
 for key in ('HOME','XDG_CONFIG_HOME','XDG_CACHE_HOME','XDG_RUNTIME_DIR'):
  path=out/key.lower();path.mkdir(mode=0o700);env[key]=str(path)
 runner=s.Runner(out,env);result={'status':'FAIL','source_commit':source,'gcc_result_sha256':s.sha(gcc/'result.json'),'runner_sha256':s.sha(__file__),'provider_rebuilt':False,'old_java_replays':False,'router_or_profile_interoperability_claimed':False}
 try:
  vectors=out/'vectors';runner.run('lease-objects',['nice','-n','5','bash',REG/'OFFLINE-COMMANDS.sh',vectors,driver,N/'preview/native/tests/fixtures/i2p-datagram',JARS['official'],JARS['modified']],timeout=180)
  origins={};labels=['dsa-one','dsa-sixteen','ed25519-one','ed25519-sixteen']
  for reference,jar in JARS.items():
   for role in ('produce','verify'):
    log=(vectors/(reference+'-classes-'+role+'.log')).read_text()
    for klass in ('net.i2p.data.i2cp.RequestVariableLeaseSetMessage','net.i2p.data.i2cp.CreateLeaseSet2Message','net.i2p.data.LeaseSet2','net.i2p.crypto.KeyGenerator'):
     actual=re.findall(r' '+re.escape(klass)+r' source: (.+)$',log,re.M);assert actual==['file:'+str(jar)],(reference,role,klass)
   for role,marker,suffix in [('produce','PASS JAVA PRODUCED ',' request create'),('native','PASS NATIVE VERIFIED JAVA ',' request create'),('verify','PASS JAVA VERIFIED NATIVE ',' create')]:
    rows=[x for x in (vectors/(reference+'-'+role+'.log')).read_text().splitlines() if x.startswith(marker)];assert rows==[marker+label+suffix for label in labels],(reference,role)
   assert 'PASS JAVA REFUSED CHANGED LEASESET SIGNATURE' in (vectors/(reference+'-verify.log')).read_text().splitlines()
   origins[reference]={'jar_sha256':s.sha(jar),'produce_class_log_sha256':s.sha(vectors/(reference+'-classes-produce.log')),'verify_class_log_sha256':s.sha(vectors/(reference+'-classes-verify.log'))}
  assert s.source_snapshot(source)==before and all(s.sha(v['path'])==v['sha256'] for v in images.values())
  result.update(status='PASS_ACTUAL_LEASESET2_OBJECTS',cases_per_reference_per_direction=4,java_messages_per_reference=8,native_messages_per_reference=4,class_origins=origins,native_images_unchanged=True,source_unchanged=True)
 except BaseException as exc:result['failure']=repr(exc)
 finally:
  result['commands_sha256']=s.sha(out/'commands.json') if (out/'commands.json').exists() else None;s.write(out/'result.json',result)
 print(result['status'],out/'result.json',flush=True);return 0 if result['status'].startswith('PASS_') else 1
if __name__=='__main__':raise SystemExit(main())
