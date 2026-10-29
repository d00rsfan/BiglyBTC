#!/usr/bin/env python3
"""New tracker objects only; exact original class origins, no previous Java replay."""
import argparse,importlib.util,pathlib,re
R=pathlib.Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';H=pathlib.Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('batch',H/'run-r3.py');b=importlib.util.module_from_spec(spec);spec.loader.exec_module(b);s=b.s
W=R/'build/i2cp-tracker-scrape-20261029'
JARS={'official':R/'build/i2p-datagram-codec-20261029/amendments/actual-offline-interop/dependencies/i2p-2.13.0.jar','modified':R/'build/i2p-modified-core-20261029/azi2pcore_2.13.0.1.jar'}
PINS={'official':'d76b3c4c4fa7043c2a3553ab3209954e63151dfd1a90f7c020fbc58b33c89896','modified':'b0866aa843a55d9290755c7a5cc3091cda765374544914afb4d3557ae99994b5'}
def main():
 p=argparse.ArgumentParser();p.add_argument('--attempt',required=True);p.add_argument('--lane-attempt',required=True);a=p.parse_args();assert all(re.fullmatch('[A-Za-z0-9_-]+',x) for x in (a.attempt,a.lane_attempt))
 adoption=s.load(H/'root-adoption-r3.json');source=adoption['source_commit'];before=s.source_snapshot(source);gcc=H/'runs'/('gcc-'+a.lane_attempt);d=s.load(gcc/'result.json')
 assert d['status']=='PASS_PROFILE_IO_I2CP_TRACKER_COHORT' and d['source_commit']==source and s.load(gcc/'source-before.json')==before
 images=s.load(gcc/'images-before.json');assert all(s.sha(row['path'])==row['sha256'] for row in images.values());driver=images['biglybtc-i2cp-tracker-codec-tests']['path']
 m=s.load(W/'manifest.json');assert s.sha(W/'manifest.json')=='faf50da882975c4b3aba7b014d64af5e98c158d7d18df8942d801d3655aee59f'
 for name,pin in m['artifacts_sha256'].items():assert s.sha(W/name)==pin
 for reference,jar in JARS.items():assert s.sha(jar)==PINS[reference]
 out=H/'offline-runs'/a.attempt;out.mkdir(parents=True,exist_ok=False);env=s.environment('gcc')
 for key in ('JAVA_TOOL_OPTIONS','_JAVA_OPTIONS','JDK_JAVA_OPTIONS','CLASSPATH','JAVA','JAVAC','ASAN_OPTIONS','LSAN_OPTIONS','UBSAN_OPTIONS','BIGLYBTC_PREVIEW_PROFILE'):env.pop(key,None)
 env['JAVA_TOOL_OPTIONS']='-XX:ActiveProcessorCount=2'
 for key in ('HOME','XDG_CONFIG_HOME','XDG_CACHE_HOME','XDG_RUNTIME_DIR'):
  path=out/key.lower();path.mkdir(mode=0o700);env[key]=str(path)
 runner=s.Runner(out,env);result={'status':'FAIL','source_commit':source,'gcc_result_sha256':s.sha(gcc/'result.json'),'runner_sha256':s.sha(__file__),'provider_rebuilt':False,'old_java_replays':False,'real_router_or_profile_claimed':False}
 try:
  vectors=out/'vectors';runner.run('tracker-objects',['nice','-n','5','bash',W/'OFFLINE-COMMANDS.sh',vectors,driver,N/'preview/native/tests/fixtures/i2p-datagram',JARS['official'],JARS['modified']],timeout=180)
  origins={};labels=['dsa','ed25519']
  for reference,jar in JARS.items():
   for role in ('produce','verify'):
    log=(vectors/(reference+'-classes-'+role+'.log')).read_text()
    for klass in ('net.i2p.data.i2cp.HostLookupMessage','net.i2p.data.i2cp.HostReplyMessage','net.i2p.data.i2cp.SendMessageExpiresMessage','net.i2p.data.i2cp.MessagePayloadMessage','net.i2p.data.DataHelper','net.i2p.client.datagram.Datagram2','net.i2p.client.datagram.Datagram3'):
     actual=re.findall(r' '+re.escape(klass)+r' source: (.+)$',log,re.M);assert actual==['file:'+str(jar)],(reference,role,klass)
   for role,marker in [('produce','PASS JAVA PRODUCED '),('native','PASS NATIVE VERIFIED JAVA '),('verify','PASS JAVA VERIFIED NATIVE ')]:
    rows=[line for line in (vectors/(reference+'-'+role+'.log')).read_text().splitlines() if line.startswith('PASS ')]
    assert rows==[marker+label+' tracker objects' for label in labels],(reference,role)
   origins[reference]={'jar_sha256':s.sha(jar),'produce_class_log_sha256':s.sha(vectors/(reference+'-classes-produce.log')),'verify_class_log_sha256':s.sha(vectors/(reference+'-classes-verify.log'))}
  assert s.source_snapshot(source)==before and all(s.sha(row['path'])==row['sha256'] for row in images.values())
  result.update(status='PASS_ACTUAL_I2CP_TRACKER_OBJECTS',cases_per_reference_per_direction=2,message_frames_per_reference_per_direction=10,class_origins=origins,native_images_unchanged=True,source_unchanged=True)
 except BaseException as exc:result['failure']=repr(exc)
 finally:
  result['commands_sha256']=s.sha(out/'commands.json') if (out/'commands.json').exists() else None;s.write(out/'result.json',result)
 print(result['status'],out/'result.json',flush=True);return 0 if result['status'].startswith('PASS_') else 1
if __name__=='__main__':raise SystemExit(main())
