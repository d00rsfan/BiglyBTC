#!/usr/bin/env python3
"""Root-only exact-current-image offline I2CP bridge; no native build or session."""
import hashlib, importlib.util, json, pathlib, re, urllib.parse

R=pathlib.Path('/home/yurii/Java/BiglyBT'); N=R.parent/'BiglyBTC'; H=pathlib.Path(__file__).parent
C=R/'build/i2cp-session-codec-20261029/amendments/allocation-failure-poison'
CAPSULE='96f471b5ebc464fa95dd350d39d98c4e68153aade0dc84e72acf2ea60a67f361'
SOURCE='ef494c64add015ea84df93ebcfbff1c7a38fa10b'
spec=importlib.util.spec_from_file_location('s',R/'build/mainline-scrape-20261028/recipe_support_followup.py')
s=importlib.util.module_from_spec(spec); spec.loader.exec_module(s)
GCC=H/'runs/gcc-r1'; CACHE=N/'build/persistent-peer-accounting-20261021/bound-gcc'
TARGETS=['biglybtc-mainline-single-http-tests','biglybtc-managed-queue-tests','biglybtc-queue-policy-tests',
 'biglybtc-mainline-single-tracker-owner-tests','biglybtc-i2cp-protocol-tests','BiglyBTC','BiglyBTC-headless','biglybtc-preview-smoke']
CASES={'get-date-none':(32,5),'get-date-empty':(32,6),'get-date-version':(32,12),'get-date-maximum':(32,261),
 'set-date-legacy':(33,13),'set-date-version':(33,20),'set-date-zero':(33,14),'disconnect-control':(30,10),
 'disconnect-maximum':(30,261),'status-created':(20,8),'status-refused':(20,8),'destroy-session':(3,7)}
REFERENCES={
 'official':(R/'build/i2p-datagram-codec-20261029/amendments/actual-offline-interop/dependencies/i2p-2.13.0.jar','d76b3c4c4fa7043c2a3553ab3209954e63151dfd1a90f7c020fbc58b33c89896'),
 'modified':(R/'build/i2p-modified-core-20261029/azi2pcore_2.13.0.1.jar','b0866aa843a55d9290755c7a5cc3091cda765374544914afb4d3557ae99994b5')}
REQUIRED={'net.i2p.data.i2cp.'+name for name in ('I2CPMessage','I2CPMessageImpl','I2CPMessageHandler',
 'GetDateMessage','SetDateMessage','DisconnectMessage','SessionStatusMessage','DestroySessionMessage','SessionId')}

def check_capsule(path,pin):
 assert s.sha(path)==pin,'Frozen recipe changed'
 data=s.load(path)
 for leaf,want in data['artifacts_sha256'].items(): assert s.sha(path.parent/leaf)==want,leaf
 return data

def framed(path,kind,size=None):
 data=path.read_bytes(); assert len(data)>=5 and len(data)<=131077
 assert int.from_bytes(data[:4],'big')==len(data)-5 and data[4]==kind,'Actual framed length/type differs'
 if size is not None: assert len(data)==size,'Actual message width differs'
 return data

def authentication_options(path):
 payload=framed(path,32)[5:]
 assert payload[:7]==b'\x062.13.0' and len(payload)>=9
 count=int.from_bytes(payload[7:9],'big'); mapping=payload[9:]
 assert count==len(mapping) and 0<count<=1024
 fields={}; pos=0
 while pos<len(mapping):
  assert len(fields)<2
  size=mapping[pos]; pos+=1; assert pos+size<=len(mapping)
  key=mapping[pos:pos+size]; pos+=size
  assert pos<len(mapping) and mapping[pos]==ord('='); pos+=1
  assert pos<len(mapping); size=mapping[pos]; pos+=1; assert pos+size<len(mapping)
  value=mapping[pos:pos+size]; pos+=size
  assert mapping[pos]==ord(';'); pos+=1
  assert key not in fields; fields[key]=value
 assert fields=={b'i2cp.username':b'known-test-user',b'i2cp.password':b'known-test-value'}
 return {'sha256':s.sha(path),'framed_bytes':path.stat().st_size,'original_properties':2,
  'native_expected_error':'Unsupported','actual_refusal_assertion_source_sha256':manifest['source_paths']['preview/native/tests/i2cp_protocol_tests.cpp']['after_sha256']}

def classes(path,jar,pin):
 all_rows=re.findall(r'\[class,load\] (net\.i2p\.\S+) source: (.+)$',path.read_text(),re.M)
 assert all_rows,'No actual original class origins'
 protocol=[row for row in all_rows if row[0].startswith('net.i2p.data.i2cp.')]
 assert REQUIRED<={name for name,_ in protocol},'Actual I2CP implementation classes missing'
 actual_paths=set()
 for name,origin in all_rows:
  uri=urllib.parse.urlparse(origin.strip())
  assert uri.scheme=='file' and not uri.netloc and not uri.query and not uri.fragment,(name,'Unexpected class origin')
  actual=pathlib.Path(urllib.parse.unquote(uri.path))
  actual_paths.add(actual.resolve())
 assert actual_paths=={jar.resolve()} and s.sha(jar)==pin,'Wrong reference JAR'
 return {'classes':len(all_rows),'i2cp_classes':sorted({name for name,_ in protocol}),
  'jar_sha256':pin,'class_log_sha256':s.sha(path)}

out=H/'offline-wrapper-r1'; out.mkdir(exist_ok=False)
run=H/'offline-r1'; result={'status':'FAIL','source_commit':SOURCE,'bridge_manifest_sha256':CAPSULE,
 'connected_session':False,'router_activated':False,'native_compilation_executed':False,
 'full_native_cohort_pass_inferred':False,'wrapper_sha256':s.sha(__file__)}
try:
 manifest=check_capsule(C/'manifest.json',CAPSULE)
 check_capsule(pathlib.Path(manifest['parent_manifest']),manifest['parent_manifest_sha256'])
 adoption=s.load(H/'root-adoption.json'); assert adoption['source_commit']==SOURCE and adoption['source_path_count']==10
 before=s.source_snapshot(SOURCE); assert s.load(GCC/'source-before.json')==before,'GCC source differs'
 for name,row in adoption['source_paths'].items(): assert s.sha(N/name)==row['after_sha256'],name
 for name,row in manifest['source_paths'].items(): assert s.sha(N/name)==row['after_sha256'],name
 commands=s.load(GCC/'commands.json'); builds=[row for row in commands if row['name']=='build']
 assert len(builds)==1 and builds[0]['exit']==0 and s.sha(builds[0]['log'])==builds[0]['log_sha256'],'Wait for unique completed GCC build'
 assert builds[0]['command']==['nice','-n','5','cmake','--build',str(CACHE),'--parallel','1','--target',*TARGETS]
 images=s.load(GCC/'images-before.json'); assert set(images)==set(TARGETS) and len(images)==8
 paths={name:(CACHE/'app'/name if name in ('BiglyBTC','BiglyBTC-headless') else CACHE/name if name=='biglybtc-preview-smoke' else CACHE/'tests'/name) for name in TARGETS}
 for name,path in paths.items(): assert pathlib.Path(images[name]['path']).resolve()==path.resolve(),name
 assert s.images(paths)==images,'Current eight-image graph differs from completed build'
 image=paths['biglybtc-i2cp-protocol-tests']; image_pin=s.sha(image)
 with image.open('rb') as stream: assert stream.read(4)==b'\x7fELF'
 for jar,pin in REFERENCES.values(): assert s.sha(jar)==pin
 s.write(out/'gcc-build-command.json',builds[0]); s.write(out/'gcc-images-before.json',images)
 result.update(native_test_elf=str(image),native_test_elf_sha256=image_pin,native_test_elf_bytes=image.stat().st_size,
  gcc_source_before_sha256=s.sha(GCC/'source-before.json'),gcc_images_before_sha256=s.sha(GCC/'images-before.json'),
  gcc_build_log_sha256=builds[0]['log_sha256'],gcc_build_command_sha256=s.sha(out/'gcc-build-command.json'),
  source_receipt_sha256=s.sha(H/'root-adoption.json'),actual_gcc_image_count=8)
 runner=s.Runner(out,s.environment('gcc'))
 runner.run('offline',['bash',C/'OFFLINE-COMMANDS.sh',image,run],timeout=180)
 assert (run/'STATUS.txt').read_text()=='PASS actual official + modified offline I2CP message bridge (12 cases each)\n'
 origins={}; options={}; exported={}
 for ref,(jar,pin) in REFERENCES.items():
  origins[ref]={}
  for stage in ('produce','verify'):
   text=(run/'logs'/(ref+'-'+stage+'.log')).read_text()
   rows=re.findall(r'^PASS original I2CP '+stage+r' (\S+) bytes=(\d+)$',text,re.M)
   assert len(rows)==12 and {name:int(size) for name,size in rows}=={name:size for name,(_,size) in CASES.items()}
   assert text.count('PASS offline original I2CP message bridge; no session/router\n')==1
   origins[ref][stage]=classes(run/(ref+'-'+stage+'-classes.log'),jar,pin)
  text=(run/'logs'/(ref+'-native.log')).read_text()
  rows=re.findall(r'^PASS actual original/native I2CP (\S+)$',text,re.M)
  assert len(rows)==12 and set(rows)==set(CASES) and text.count('PASS bounded I2CP framing/initial session subset; no connection\n')==1
  assert (run/ref/'greeting.java').read_bytes()==b'\x2a'
  options[ref]=authentication_options(run/ref/'unsupported-auth.java'); exported[ref]={}
  for name,(kind,size) in CASES.items():
   java=framed(run/ref/(name+'.java'),kind,size); native=framed(run/ref/(name+'.native'),kind,size)
   assert native==java,'Native/original export differs: '+name
   exported[ref][name]=hashlib.sha256(native).hexdigest()
 assert exported['official']==exported['modified'],'Deterministic message bytes differ across references'
 assert (run/'inputs-before.sha256').read_bytes()==(run/'inputs-after.sha256').read_bytes()
 input_rows=re.findall(r'^([0-9a-f]{64})  (/.+)$',(run/'inputs-after.sha256').read_text(),re.M)
 expected_inputs={image,pathlib.Path('/usr/lib/jvm/java-17-openjdk-amd64/bin/java'),pathlib.Path('/usr/lib/jvm/java-17-openjdk-amd64/bin/javac'),C/'interop/I2cpVectors.java',*[jar for jar,_ in REFERENCES.values()]}
 assert len(input_rows)==6 and {pathlib.Path(path) for _,path in input_rows}==expected_inputs
 for pin,path in input_rows: assert s.sha(path)==pin
 assert [row for row in s.load(GCC/'commands.json') if row['name']=='build']==builds,'GCC build receipt changed'
 assert s.sha(image)==image_pin and s.images(paths)==images and s.source_snapshot(SOURCE)==before
 result.update(status='PASS_ACTUAL_I2CP_INITIAL_SESSION_OFFLINE',cases_per_reference=12,actual_reference_origins=origins,
  exact_deterministic_exports_sha256=exported,actual_auth_options_refusal=options,
  files_sha256={str(path.relative_to(run)):s.sha(path) for path in run.rglob('*') if path.is_file()},
  source_unchanged=True,all_eight_images_unchanged=True,one_javac_serial=True)
except BaseException as error: result['failure']=repr(error)
finally:
 if (out/'commands.json').exists(): result['commands_sha256']=s.sha(out/'commands.json')
 s.write(out/'result.json',result)
print(result['status'],out/'result.json',flush=True)
raise SystemExit(0 if result['status'].startswith('PASS_') else 1)
