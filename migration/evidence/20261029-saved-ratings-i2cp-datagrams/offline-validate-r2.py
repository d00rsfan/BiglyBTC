#!/usr/bin/env python3
"""Validate successful r2 object executions; correct report-prefix collision without replay."""
import importlib.util,pathlib,re,shutil
H=pathlib.Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('offline',H/'offline-r2.py');o=importlib.util.module_from_spec(spec);spec.loader.exec_module(o);s=o.s
def main():
 old=H/'offline-runs/r2';out=H/'offline-runs/r2-validation';out.mkdir(exist_ok=False)
 failed=s.load(old/'result.json');adoption=s.load(H/'root-adoption-r2.json');source=adoption['source_commit']
 result={'status':'FAIL','source_commit':source,'runner_sha256':s.sha(__file__),'original_failed_result_sha256':s.sha(old/'result.json'),'report_only_correction':'Separate eight send rows from the ninth status/payload summary sharing its prefix.','executions_replayed':False,'provider_rebuilt':False,'old_java_replays':False,'router_or_profile_interoperability_claimed':False}
 try:
  assert failed['status']=='FAIL' and failed['failure']=="AssertionError(('official', 'native'))" and failed['source_commit']==source
  commands=s.load(old/'commands.json');assert len(commands)==1 and commands[0]['name']=='datagram-objects' and commands[0]['exit']==0 and 'exception' not in commands[0]
  assert s.sha(old/'commands.json')==failed['commands_sha256']
  for row in commands:assert s.sha(row['log'])==row['log_sha256']
  before=s.source_snapshot(source);gcc=H/'runs/gcc-r2';d=s.load(gcc/'result.json');assert d['status']=='PASS_SAVED_RATING_DATAGRAM_COHORT' and d['source_commit']==source and s.load(gcc/'source-before.json')==before
  images=s.load(gcc/'images-before.json');assert all(s.sha(v['path'])==v['sha256'] for v in images.values())
  for name,row in adoption['source_paths'].items():assert s.sha(o.N/name)==row['after_sha256']
  m=s.load(o.REG/'manifest.json');assert s.sha(o.REG/'manifest.json')=='a72cbacb75b4b431f173f5c7262e634aad4b25e9285d012660d06e65b17529d8'
  for name,pin in m['artifacts_sha256'].items():assert s.sha(o.REG/name)==pin
  vectors=old/'vectors';raw={str(p.relative_to(old)):s.sha(p) for p in old.rglob('*') if p.is_file()}
  s.write(out/'original-output-inventory.json',raw)
  labels=[family+'-'+kind+'-'+mode for family in ('dsa','ed25519') for kind in ('2','3') for mode in ('stored','deflate')];origins={}
  for reference,jar in o.JARS.items():
   assert s.sha(jar)==o.PINS[reference]
   for role in ('produce','verify'):
    log=(vectors/(reference+'-classes-'+role+'.log')).read_text()
    for klass in ('net.i2p.data.i2cp.SendMessageMessage','net.i2p.data.i2cp.SendMessageExpiresMessage','net.i2p.data.i2cp.MessageStatusMessage','net.i2p.data.i2cp.MessagePayloadMessage','net.i2p.data.DataHelper','net.i2p.client.datagram.Datagram2','net.i2p.client.datagram.Datagram3'):
     actual=re.findall(r' '+re.escape(klass)+r' source: (.+)$',log,re.M);assert actual==['file:'+str(jar)],(reference,role,klass)
   for role,marker in [('produce','PASS JAVA PRODUCED '),('native','PASS NATIVE VERIFIED JAVA '),('verify','PASS JAVA VERIFIED NATIVE ')]:
    rows=[x for x in (vectors/(reference+'-'+role+'.log')).read_text().splitlines() if x.startswith('PASS ')]
    expected=[marker+label+' send' for label in labels]
    if role!='produce':expected.append(marker+'FOUR STATUSES AND INBOUND PAYLOAD')
    assert rows==expected,(reference,role)
   origins[reference]={'jar_sha256':s.sha(jar),'produce_class_log_sha256':s.sha(vectors/(reference+'-classes-produce.log')),'verify_class_log_sha256':s.sha(vectors/(reference+'-classes-verify.log'))}
  assert {str(p.relative_to(old)):s.sha(p) for p in old.rglob('*') if p.is_file()}==raw
  assert s.source_snapshot(source)==before and all(s.sha(v['path'])==v['sha256'] for v in images.values())
  shutil.copyfile(old/'commands.json',out/'commands.json')
  result.update(status='PASS_ACTUAL_DATAGRAM_SUBMISSION_OBJECTS',gcc_result_sha256=s.sha(gcc/'result.json'),send_cases_per_reference_per_direction=8,status_cases_per_reference_per_direction=4,payload_cases_per_reference_per_direction=1,class_origins=origins,native_images_unchanged=True,source_unchanged=True,commands_sha256=s.sha(out/'commands.json'),original_output_inventory_sha256=s.sha(out/'original-output-inventory.json'),original_execution_directory=str(old))
 except BaseException as exc:result['failure']=repr(exc)
 finally:s.write(out/'result.json',result)
 print(result['status'],out/'result.json');return 0 if result['status'].startswith('PASS_') else 1
if __name__=='__main__':raise SystemExit(main())
