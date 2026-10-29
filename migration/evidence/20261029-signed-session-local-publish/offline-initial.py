#!/usr/bin/env python3
"""Root finite current-source original-object comparisons; no router/session connect."""
import argparse,importlib.util,os,pathlib,re
R=pathlib.Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';H=pathlib.Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('batch',H/'run.py');batch=importlib.util.module_from_spec(spec);spec.loader.exec_module(batch);s=batch.s
REG=R/'build/i2cp-session-registration-20261029';PUB=R/'build/azureus-local-publish-20261029/amendments/publication-authority/amendments/close-commit'
JARS={'official':R/'build/i2p-datagram-codec-20261029/amendments/actual-offline-interop/dependencies/i2p-2.13.0.jar','modified':R/'build/i2p-modified-core-20261029/azi2pcore_2.13.0.1.jar'}
JARPINS={'official':'d76b3c4c4fa7043c2a3553ab3209954e63151dfd1a90f7c020fbc58b33c89896','modified':'b0866aa843a55d9290755c7a5cc3091cda765374544914afb4d3557ae99994b5'}
def main():
 p=argparse.ArgumentParser();p.add_argument('--attempt',required=True);p.add_argument('--lane-attempt',required=True);a=p.parse_args();assert all(re.fullmatch('[A-Za-z0-9_-]+',x) for x in (a.attempt,a.lane_attempt))
 adoption=s.load(H/'root-adoption.json');source=adoption['source_commit'];before=s.source_snapshot(source);gcc=H/'runs'/('gcc-'+a.lane_attempt);d=s.load(gcc/'result.json');assert d['status']=='PASS_SIGNED_SESSION_LOCAL_PUBLISH_COHORT' and d['lane']=='gcc' and d['source_commit']==source and s.load(gcc/'source-before.json')==before
 images=s.load(gcc/'images-before.json');assert all(s.sha(v['path'])==v['sha256'] for v in images.values())
 for row in adoption['source_capsules']:
  path=pathlib.Path(row['path']);assert s.sha(path)==row['sha256'];m=s.load(path)
  for name,pin in m.get('artifacts',m.get('artifacts_sha256',{})).items():assert s.sha(path.parent/name)==pin
 for name,path in JARS.items():assert s.sha(path)==JARPINS[name]
 out=H/'offline-runs'/a.attempt;out.mkdir(parents=True,exist_ok=False);env=s.environment('gcc')
 for key in ('JAVA_TOOL_OPTIONS','_JAVA_OPTIONS','JDK_JAVA_OPTIONS','CLASSPATH','JAVA','JAVAC','ASAN_OPTIONS','LSAN_OPTIONS','UBSAN_OPTIONS','BIGLYBTC_PREVIEW_PROFILE','BIGLYBTC_HTTP_PRIVATE_RUNTIME'):env.pop(key,None)
 env['JAVA_TOOL_OPTIONS']='-XX:ActiveProcessorCount=2'
 for key in ('HOME','XDG_CONFIG_HOME','XDG_CACHE_HOME','XDG_RUNTIME_DIR'):
  path=out/key.lower();path.mkdir(mode=0o700);env[key]=str(path)
 runner=s.Runner(out,env);result={'status':'FAIL','source_commit':source,'gcc_result_sha256':s.sha(gcc/'result.json'),'runner_sha256':s.sha(__file__),'provider_rebuilt':False,'old_java_replays':False,'router_or_profile_interoperability_claimed':False}
 try:
  driver=pathlib.Path(images['biglybtc-i2cp-session-config-tests']['path']);vectors=out/'session-vectors'
  runner.run('session-objects',['nice','-n','5','bash',REG/'OFFLINE-COMMANDS.sh',vectors,driver,N/'preview/native/tests/fixtures/i2p-datagram',JARS['official'],JARS['modified']],timeout=180)
  origins={}
  for reference,jar in JARS.items():
   for role in ('produce','verify'):
    log=(vectors/(reference+'-classes-'+role+'.log')).read_text()
    for klass in ('net.i2p.data.i2cp.SessionConfig','net.i2p.data.i2cp.CreateSessionMessage','net.i2p.data.i2cp.ReconfigureSessionMessage','net.i2p.crypto.DSAEngine'):
     origins_for_class=re.findall(r' '+re.escape(klass)+r' source: (.+)$',log,re.M);assert origins_for_class==['file:'+str(jar)],(reference,role,klass)
   labels=['dsa-empty','dsa-utf8','dsa-maximum','ed25519-empty','ed25519-utf8','ed25519-maximum'];suffixes=[label+' '+kind for label in labels for kind in ('create','reconfigure')]
   for role,marker in [('produce','PASS JAVA PRODUCED '),('native','PASS NATIVE VERIFIED JAVA '),('verify','PASS JAVA VERIFIED NATIVE ')]:
    actual=[line for line in (vectors/(reference+'-'+role+'.log')).read_text().splitlines() if line.startswith(marker)];assert actual==[marker+suffix for suffix in suffixes],(reference,role,'actual message inventory')
   assert 'PASS NATIVE REFUSED ORIGINAL AUTH OPTIONS' in (vectors/(reference+'-native.log')).read_text().splitlines()
   assert 'PASS JAVA REFUSED CHANGED SIGNATURE AND VALIDLY SIGNED STALE DATE' in (vectors/(reference+'-verify.log')).read_text().splitlines()
   origins[reference]={'jar_sha256':s.sha(jar),'produce_class_log_sha256':s.sha(vectors/(reference+'-classes-produce.log')),'verify_class_log_sha256':s.sha(vectors/(reference+'-classes-verify.log'))}
  fieldsout=PUB/'oracle/runs'/('run-'+a.attempt);assert not fieldsout.exists()
  runner.run('original-local-value-fields',['nice','-n','5','python3','-B',PUB/'oracle/run.py','--driver',images['biglybtc-azureus-database-runtime-tests']['path'],'--source-commit',source,'--build-receipt',gcc/'build-receipt.json','--build-source-map',gcc/'native-source-map.json','--attempt','run-'+a.attempt],timeout=180)
  fields=s.load(fieldsout/'result.json');assert fields['status']=='PASS_RETAINED_ORIGINAL_LOCAL_VALUE_VERSION_FIELDS' and fields['source_commit']==source and fields['actual_version_cases']==3 and fields['actual_value_cases']==1
  assert s.source_snapshot(source)==before and all(s.sha(v['path'])==v['sha256'] for v in images.values())
  result.update(status='PASS_ACTUAL_SESSION_OBJECTS_AND_LOCAL_VALUE_FIELDS',session_configs_per_reference=6,session_messages_per_reference_per_direction=12,session_class_origins=origins,local_value_fields_result={'path':str(fieldsout/'result.json'),'sha256':s.sha(fieldsout/'result.json')},native_images_unchanged=True,source_unchanged=True)
 except BaseException as exc:result['failure']=repr(exc)
 finally:
  result['commands_sha256']=s.sha(out/'commands.json') if (out/'commands.json').exists() else None;s.write(out/'result.json',result)
 print(result['status'],out/'result.json',flush=True);return 0 if result['status'].startswith('PASS_') else 1
if __name__=='__main__':raise SystemExit(main())
