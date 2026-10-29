#!/usr/bin/env python3
"""Execute the frozen unconnected original identity bridge against both exact cores."""
import hashlib,importlib.util,json,pathlib,re
R=pathlib.Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';H=pathlib.Path(__file__).parent
C=R/'build/i2p-client-identity-20261029/amendments/offline-fixture-bridge';M=C/'manifest.json'
spec=importlib.util.spec_from_file_location('s',R/'build/mainline-scrape-20261028/recipe_support_followup.py');s=importlib.util.module_from_spec(spec);spec.loader.exec_module(s)
assert s.sha(M)=='1502245f6e23427be40142128906725f88c62589513bc24ecc82fb9557307bfb'
for name,pin in s.load(M)['artifacts_sha256'].items():assert s.sha(C/name)==pin
source=s.load(H/'root-adoption.json')['source_commit'];before=s.source_snapshot(source)
gcc=H/'runs/gcc-r1';commands=s.load(gcc/'commands.json');builds=[x for x in commands if x['name']=='build']
assert len(builds)==1 and builds[0]['exit']==0 and s.sha(builds[0]['log'])==builds[0]['log_sha256'],'Wait for exact completed GCC build'
assert s.load(gcc/'source-before.json')==before,'GCC source differs'
cache=N/'build/persistent-peer-accounting-20261021/bound-gcc';image=cache/'tests/biglybtc-i2p-client-identity-tests';image_pin=s.sha(image)
qualified=s.load(gcc/'images-before.json')['biglybtc-i2p-client-identity-tests']
assert pathlib.Path(qualified['path']).resolve()==image.resolve() and qualified['sha256']==image_pin and qualified['bytes']==image.stat().st_size
out=H/'offline-wrapper-r1';out.mkdir(exist_ok=False);env=s.environment('gcc');runner=s.Runner(out,env);run=H/'offline-r1';result={'status':'FAIL','source_commit':source,'native_test_elf_sha256':image_pin,'bridge_manifest_sha256':s.sha(M),'gcc_source_before_sha256':s.sha(gcc/'source-before.json'),'gcc_images_before_sha256':s.sha(gcc/'images-before.json'),'gcc_build_log_sha256':builds[0]['log_sha256'],'connected_session':False}
try:
 runner.run('offline',['bash',C/'OFFLINE-COMMANDS.sh',image,run],timeout=180)
 assert (run/'STATUS.txt').read_text()=='PASS actual official + modified offline identity import/export/signature cases (4 each)\n'
 expected={'dsa-default','dsa-key-certificate','ed25519-default','ed25519-zero-encryption'};origins={}
 for ref,jar_pin in [('official','d76b3c4c4fa7043c2a3553ab3209954e63151dfd1a90f7c020fbc58b33c89896'),('modified','b0866aa843a55d9290755c7a5cc3091cda765374544914afb4d3557ae99994b5')]:
  origins[ref]={}
  for stage in ('produce','verify'):
   text=(run/'logs'/(ref+'-'+stage+'.log')).read_text();rows=re.findall(r'^PASS original online client identity '+stage+r' (\S+) bytes=(\d+)$',text,re.M)
   assert len(rows)==4 and {x[0] for x in rows}==expected and {int(x[1]) for x in rows}=={663,667,679}
   log=(run/(ref+'-'+stage+'-classes.log')).read_text();loaded=re.findall(r'\[class,load\] (net\.i2p\.\S+) source: file:(\S+)',log)
   assert {'net.i2p.client.impl.I2PSessionImpl','net.i2p.client.datagram.Datagram2'}<={x[0] for x in loaded}
   paths={pathlib.Path(x[1]) for x in loaded};assert len(paths)==1 and s.sha(next(iter(paths)))==jar_pin
   origins[ref][stage]={'classes':len(loaded),'jar_sha256':jar_pin,'class_log_sha256':s.sha(run/(ref+'-'+stage+'-classes.log'))}
  native=(run/'logs'/(ref+'-native.log')).read_text();rows=re.findall(r'^PASS actual Java online identity import/export/signature (\S+)$',native,re.M);assert len(rows)==4 and set(rows)==expected
 for label in expected:
  for suffix in ('java.keys','native.keys','verified.keys'):assert s.sha(run/'official'/(label+'.'+suffix))==s.sha(run/'modified'/(label+'.'+suffix))
 assert s.sha(image)==image_pin and s.source_snapshot(source)==before
 result.update(status='PASS_ACTUAL_I2P_IDENTITY_OFFLINE',cases_per_reference=4,actual_reference_origins=origins,files_sha256={str(p.relative_to(run)):s.sha(p) for p in run.rglob('*') if p.is_file()},source_unchanged=True)
except BaseException as exc:result['failure']=repr(exc)
finally:s.write(out/'result.json',result)
print(result['status'],out/'result.json',flush=True)
raise SystemExit(0 if result['status'].startswith('PASS_') else 1)
