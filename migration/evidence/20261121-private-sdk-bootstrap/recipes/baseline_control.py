from pathlib import Path
import hashlib,json,os,subprocess
R=Path('/home/yurii/Java/BiglyBT');D=Path(__file__).resolve().parent;W=R/'build/public-nat-20261114'
exe=R/'build/public-shared-quota-runtime-20261121/native-gcc/tests/biglybtc-private-sdk-bootstrap-tests'
env=dict(os.environ,LD_LIBRARY_PATH=str(R/'build/tag-rate-cross-owner-20261121/sdk-install/lib')+':/opt/Qt/6.12.0/gcc_64/lib',QT_QPA_PLATFORM='offscreen')
rows=[]
for mode in ['direct','service']:
 result=subprocess.run([str(exe),mode],env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=45)
 log=W/('private-bootstrap-before-'+mode+'.log');log.write_text(result.stdout)
 assert result.returncode==1 and 'BOOTSTRAP_OBSERVED mode='+mode in result.stdout,(mode,result.returncode,result.stdout[-1800:])
 assert 'storage_untouched=0' in result.stdout,(mode,result.stdout[-1800:])
 if mode=='service':assert 'source_gate_released=0' in result.stdout,result.stdout[-1800:]
 rows.append(dict(mode=mode,expected_exit=1,actual_exit=result.returncode,log_sha256=hashlib.sha256(log.read_bytes()).hexdigest()))
(D/'before-controls.json').write_text(json.dumps(rows,indent=2)+'\n')
print('PASS_BASELINE_BOOTSTRAP_DEFECTS actual constructor observes storage already prepared in both modes and held source gate in service mode')
