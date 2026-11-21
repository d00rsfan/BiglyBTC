from pathlib import Path
import hashlib,json,os,resource,subprocess
D=Path(__file__).resolve().parent;R=D.parents[1];W=R/'build/public-nat-20261114';resource.setrlimit(resource.RLIMIT_CORE,(0,0));env=dict(os.environ);env['LD_LIBRARY_PATH']=str(R/'build/tag-rate-cross-owner-20261121/sdk-install/lib')+':/opt/Qt/6.12.0/gcc_64/lib'
results=[]
for mode in ['abandoned','abandoned-installed']:
 log=W/f'profile-preadd-before-{mode}.log';argv=['timeout','40',str(D/'probe-gcc/profile-pending-controls'),mode]
 with log.open('w') as out:result=subprocess.run(argv,env=env,stdout=out,stderr=subprocess.STDOUT)
 row=dict(argv=argv,returncode=result.returncode,expected=1,log_sha256=hashlib.sha256(log.read_bytes()).hexdigest(),source_sha256=hashlib.sha256((D/'before-abandoned-profile.cpp').read_bytes()).hexdigest(),actual_message=log.read_text().strip());results.append(row)
 assert result.returncode==1 and 'Abandoned pending reservation blocks retry' in log.read_text(),row
(D/'before-abandoned-receipts.json').write_text(json.dumps(results,indent=2)+'\n')
print('PASS_RECORDED_PROFILE_PREADD_ABANDONED two actual expected exit1 controls;fresh and installed callbacks block retry before fix')
