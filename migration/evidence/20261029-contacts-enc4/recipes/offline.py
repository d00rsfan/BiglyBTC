#!/usr/bin/env python3
"""Root-only Enc4 object interop; importing performs no subjects or writes."""
import argparse, importlib.util, json, os, pathlib, re, shutil, struct
R=pathlib.Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';H=pathlib.Path(__file__).resolve().parent
SUPPORT=R/'build/mainline-scrape-20261028/recipe_support_followup.py'
spec=importlib.util.spec_from_file_location('support',SUPPORT);s=importlib.util.module_from_spec(spec);spec.loader.exec_module(s)
W=R/'build/i2cp-enc4-20261029';CAP=W/'amendments/owned-encryption-test-column/manifest.json'
CAP_SHA='0f9487d72e2524b9f7a9fb1d58f1ea9abbca561a8a4946b6e090a1e424cdb4cf'
JARS={'official':R/'build/i2p-datagram-codec-20261029/amendments/actual-offline-interop/dependencies/i2p-2.13.0.jar','modified':R/'build/i2p-modified-core-20261029/azi2pcore_2.13.0.1.jar'}
JAR_SHA={'official':'d76b3c4c4fa7043c2a3553ab3209954e63151dfd1a90f7c020fbc58b33c89896','modified':'b0866aa843a55d9290755c7a5cc3091cda765374544914afb4d3557ae99994b5'}
LABELS=['dsa-one','dsa-sixteen','ed25519-one','ed25519-sixteen'];TARGET='biglybtc-i2cp-leaseset-tests'
FIXTURES=N/'preview/native/tests/fixtures/i2p-datagram'
def files(paths):
 return {str(p):{'sha256':s.sha(p),'bytes':p.stat().st_size} for p in sorted({pathlib.Path(x).resolve() for x in paths})}
def tree(root):return {str(p.relative_to(root)):s.sha(p) for p in sorted(root.rglob('*')) if p.is_file()}
def frame(path,kind):
 data=path.read_bytes();assert 5<=len(data)<=4096 and struct.unpack('>I',data[:4])[0]==len(data)-5 and data[4]==kind,path
def wrapper(path,tool,flags,journal):
 # Literal Python arguments go straight to execv; no shell interpolation.
 path.write_text('#!/usr/bin/python3\nimport json,os,sys\ntool='+repr(str(tool))+'\nargs='+repr(list(flags))+'+sys.argv[1:]\nfd=os.open('+repr(str(journal))+',os.O_WRONLY|os.O_CREAT|os.O_APPEND|os.O_NOFOLLOW,0o600)\ntry:\n data=(json.dumps({"tool":tool,"arguments":args})+"\\n").encode();assert os.write(fd,data)==len(data)\nfinally:os.close(fd)\nos.execv(tool,[tool,*args])\n')
 path.chmod(0o700)
def main():
 p=argparse.ArgumentParser();p.add_argument('--attempt',required=True);p.add_argument('--source-commit',required=True)
 for arg in ('gcc-result','producer','adoption'):p.add_argument('--'+arg,type=pathlib.Path,required=True)
 for tool in ('java','javac'):p.add_argument('--'+tool,type=pathlib.Path,default=pathlib.Path('/usr/bin/'+tool))
 a=p.parse_args();assert re.fullmatch('[A-Za-z0-9_-]+',a.attempt) and re.fullmatch('[0-9a-f]{40}',a.source_commit)
 assert all(x.is_absolute() for x in (a.gcc_result,a.producer,a.adoption,a.java,a.javac))
 spec=importlib.util.spec_from_file_location('batch',a.producer);b=importlib.util.module_from_spec(spec);spec.loader.exec_module(b)
 source=a.source_commit;before=s.source_snapshot(source);root=a.gcc_result.parent;d=s.load(a.gcc_result);adoption=s.load(a.adoption)
 assert d['status']=='PASS_ENC4_LEASESET_COHORT' and d['lane']=='gcc' and d['source_commit']==source==adoption['source_commit']
 assert d['source_unchanged'] and d['images_unchanged'] and d['runner_sha256']==s.sha(a.producer) and d['source_receipt_sha256']==s.sha(a.adoption)
 assert s.load(root/'source-before.json')==s.load(root/'source-after.json')==before and s.load(root/'native-source-map.json')==before['files_sha256']
 for name,row in adoption['source_paths'].items():assert s.sha(N/name)==row['after_sha256'],name
 images=s.load(root/'images-before.json');assert images==s.load(root/'images-after.json') and set(images)==set(d['compiled_targets'])
 assert all(s.sha(x['path'])==x['sha256'] and pathlib.Path(x['path']).stat().st_size==x['bytes'] for x in images.values())
 driver=pathlib.Path(images[TARGET]['path']);cache=pathlib.Path(d['cache']);assert driver.resolve()==(cache/'tests'/TARGET).resolve()
 assert s.sha(root/'commands.json')==d['commands_sha256'];commands=s.load(root/'commands.json')
 for row in commands:assert row.get('exit')==0 and 'exception' not in row and s.sha(row['log'])==row['log_sha256']
 build=[x for x in commands if x['name']=='build'];assert len(build)==1 and TARGET in build[0]['command']
 receipt=s.load(root/'build-receipt.json');assert receipt['exit']==0 and receipt['command']==build[0]['command'] and receipt['source_manifest_sha256']==s.sha(root/'native-source-map.json')
 assert s.sha(root/'tests.xml')==d['tests_xml_sha256'] and s.sha(root/'ctest-full.log')==d['full_ctest_log_sha256']
 assert d['actual_suites']==len(d['suite_names']) and 'pure-i2cp-leaseset2' in d['suite_names'];s.passed_xml(root/'tests.xml',d['suite_names'])
 registered=[x for x in s.load(root/'inventory.log')['tests'] if x['name']=='pure-i2cp-leaseset2'];assert len(registered)==1 and pathlib.Path(registered[0]['command'][0]).resolve()==driver.resolve()
 assert s.sha(CAP)==CAP_SHA;cap=s.load(CAP)
 for row in cap['effective_artifacts'].values():assert s.sha(row['path'])==row['sha256']
 for name,row in cap['source_paths'].items():assert s.sha(N/name)==row['after_sha256'],name
 for name,jar in JARS.items():assert s.sha(jar)==JAR_SHA[name]
 java=a.java.resolve(strict=True);javac=a.javac.resolve(strict=True);assert java.parent==javac.parent and os.access(java,os.X_OK) and os.access(javac,os.X_OK)
 provider=b.provider_pins();assert provider==d['provider'];assert provider['artifact_sha256']=='355f0c7bca7404c3199e85d4741a3734ac460c79d641a2539c049c5a8ad0c443'
 inputs=files([a.gcc_result,a.producer,a.adoption,SUPPORT,CAP,W/'OFFLINE-COMMANDS.sh',W/'interop/LeaseSetEnc4Vectors.java',java,javac,'/usr/bin/bash','/usr/bin/nice','/usr/bin/timeout','/usr/bin/python3',cache/'CMakeCache.txt',N/'dist/BUILD_MANIFEST.json',*JARS.values(),*(FIXTURES/(family+suffix) for family in ('dsa','ed25519') for suffix in ('.sender','.known-test-key'))])
 out=H/'offline-runs'/a.attempt;out.mkdir(parents=True,exist_ok=False);env=s.environment('gcc')
 for key in ('JAVA_TOOL_OPTIONS','_JAVA_OPTIONS','JDK_JAVA_OPTIONS','CLASSPATH','JAVA','JAVAC','LD_DEBUG_OUTPUT','ASAN_OPTIONS','LSAN_OPTIONS','UBSAN_OPTIONS','MAKEFLAGS','CMAKE_BUILD_PARALLEL_LEVEL','BIGLYBTC_PREVIEW_PROFILE','BIGLYBTC_HTTP_PRIVATE_RUNTIME','BIGLYBTC_I2P_STREAMING_ACCEPTANCE_ROOT'):env.pop(key,None)
 for key in ('HOME','TMPDIR','XDG_CONFIG_HOME','XDG_CACHE_HOME','XDG_RUNTIME_DIR'):
  path=out/key.lower();path.mkdir(mode=0o700);env[key]=str(path)
 journal=out/'tool-invocations.jsonl';wrappers=out/'tools';wrappers.mkdir(mode=0o700)
 wrapper(wrappers/'java',java,['-Xms16m','-Xmx128m','-XX:ActiveProcessorCount=1','-XX:+UseSerialGC','-Duser.home='+env['HOME'],'-Djava.io.tmpdir='+env['TMPDIR']],journal)
 wrapper(wrappers/'javac',javac,['-J-Xms16m','-J-Xmx128m','-J-XX:ActiveProcessorCount=1','-J-XX:+UseSerialGC','-J-Duser.home='+env['HOME'],'-J-Djava.io.tmpdir='+env['TMPDIR']],journal)
 env.update(JAVA=str(wrappers/'java'),JAVAC=str(wrappers/'javac'));controlled=files(wrappers.iterdir());s.write(out/'inputs.json',{'source_commit':source,'native_images':images,'provider':provider,'files':inputs,'controlled_wrappers':controlled})
 runner=s.Runner(out,env);result={'status':'FAIL','source_commit':source,'gcc_result_sha256':s.sha(a.gcc_result),'runner_sha256':s.sha(__file__),'inputs_sha256':s.sha(out/'inputs.json'),'provider_rebuilt':False,'old_java_replays':False,'router_or_real_profile_claimed':False}
 try:
  vectors=out/'vectors';log=runner.run('enc4-objects',['/usr/bin/timeout','--signal=TERM','--kill-after=5s','180s','/usr/bin/nice','-n','5','/usr/bin/bash',W/'OFFLINE-COMMANDS.sh',vectors,driver,FIXTURES,JARS['official'],JARS['modified']],timeout=195)
  assert log.read_text().splitlines()[-1]=='PASS OFFLINE ORIGINAL/NATIVE ENC4 LEASESET2 OBJECT COMMANDS'
  origins={}
  for reference,jar in JARS.items():
   for role in ('produce','verify'):
    classlog=(vectors/(reference+'-classes-'+role+'.log')).read_text();loaded=re.findall(r' (net\.i2p\.[^ ]+) source: (.+)$',classlog,re.M)
    assert loaded and all(origin=='file:'+str(jar) for _,origin in loaded),(reference,role)
    for klass in ('net.i2p.data.LeaseSet2','net.i2p.crypto.KeyGenerator','net.i2p.data.i2cp.CreateLeaseSet2Message','net.i2p.data.i2cp.RequestVariableLeaseSetMessage','net.i2p.data.Destination'):
     assert [origin for name,origin in loaded if name==klass]==['file:'+str(jar)],(reference,role,klass)
    assert re.findall(r' LeaseSetEnc4Vectors source: (.+)$',classlog,re.M)==['file:'+str(vectors/'classes')+'/']
   expected={'produce':['PASS EXACT REFERENCE CLASS ORIGINS',*('PASS JAVA PRODUCED '+x+' request create' for x in LABELS)],'native':['PASS NATIVE VERIFIED JAVA '+x+' request create' for x in LABELS],'verify':['PASS EXACT REFERENCE CLASS ORIGINS',*('PASS JAVA VERIFIED NATIVE '+x+' create' for x in LABELS),'PASS JAVA REFUSED CHANGED LEASESET SIGNATURE']}
   for role,rows in expected.items():
    text=(vectors/(reference+'-'+role+'.log')).read_text();assert [x for x in text.splitlines() if x.startswith('PASS ')]==rows and not re.search(r'^(?:FAIL|Exception)',text,re.M),(reference,role)
   for label in LABELS:
    for suffix,kind in (('.java.request',37),('.java.create',41),('.native.create',41)):frame(vectors/reference/(label+suffix),kind)
   frame(vectors/reference/'native.bad-signature.create',41)
   assert (vectors/reference/'creation.clock').stat().st_size==8
   for family in ('dsa','ed25519'):
    for suffix in ('.sender','.known-test-key'):assert s.sha(vectors/reference/(family+suffix))==s.sha(FIXTURES/(family+suffix))
   origins[reference]={'jar_sha256':s.sha(jar),'produce_class_log_sha256':s.sha(vectors/(reference+'-classes-produce.log')),'verify_class_log_sha256':s.sha(vectors/(reference+'-classes-verify.log'))}
  calls=[json.loads(x) for x in journal.read_text().splitlines()];assert len(calls)==7
  assert [x['tool'] for x in calls]==[str(java),str(javac),str(javac),str(java),str(java),str(java),str(java)]
  compiles=[x for x in calls if x['tool']==str(javac) and '--release' in x['arguments']];assert len(compiles)==1 and compiles[0]['arguments'][-1]==str(W/'interop/LeaseSetEnc4Vectors.java')
  classes=tree(vectors/'classes');assert set(classes)=={'LeaseSetEnc4Vectors.class'}
  assert s.source_snapshot(source)==before and files(inputs)==inputs and files(controlled)==controlled and b.provider_pins()==provider
  assert all(s.sha(x['path'])==x['sha256'] and pathlib.Path(x['path']).stat().st_size==x['bytes'] for x in images.values())
  s.write(out/'outputs-before-class-cleanup.json',tree(vectors));shutil.rmtree(vectors/'classes');assert not (vectors/'classes').exists()
  result.update(status='PASS_ACTUAL_ENC4_LEASESET_OBJECTS',cases_per_reference_per_direction=4,java_to_native_frames_per_reference=8,native_to_java_frames_per_reference=4,changed_signature_negatives_per_reference=1,class_origins=origins,helper_classes=classes,classes_removed_after_hashing=True,tool_invocations_sha256=s.sha(journal),outputs_before_cleanup_sha256=s.sha(out/'outputs-before-class-cleanup.json'),source_unchanged=True,native_images_unchanged=True,provider_unchanged=True)
 except BaseException as exc:result['failure']=repr(exc)
 finally:
  result['commands_sha256']=s.sha(out/'commands.json') if (out/'commands.json').exists() else None;s.write(out/'result.json',result)
 print(result['status'],out/'result.json',flush=True);return 0 if result['status'].startswith('PASS_') else 1
if __name__=='__main__':raise SystemExit(main())
