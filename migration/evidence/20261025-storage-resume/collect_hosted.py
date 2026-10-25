import argparse,json,subprocess,time
p=argparse.ArgumentParser();p.add_argument("--prefix",default="",choices=["","final-"]);a=p.parse_args()
from pathlib import Path
w=Path(__file__).resolve().parent;source=(w/'final-source-commit.txt').read_text().strip();start=time.monotonic();last=start
while True:
 data=json.loads(subprocess.check_output(['gh','run','list','--repo','d00rsfan/BiglyBTC','--commit',source,'--json','databaseId,name,status,conclusion,headSha,url','--limit','8'],text=True,timeout=30))
 (w/(a.prefix+'hosted-progress.json')).write_text(json.dumps(data,indent=2)+'\n')
 if len(data)!=2:
  assert time.monotonic()-start<120,'Hosted registration timeout'
  time.sleep(5);continue
 assert all(x['headSha']==source for x in data)
 if all(x['status']=='completed' for x in data):break
 assert time.monotonic()-start<2400,'Hosted completion timeout'
 if time.monotonic()-last>45:print('Hosted checks still running; status retained on disk',flush=True);last=time.monotonic()
 time.sleep(20)
(w/(a.prefix+'hosted-final.json')).write_text(json.dumps(data,indent=2)+'\n')
for x in data:
 with (w/('hosted-'+str(x['databaseId'])+'.log')).open('w') as log:subprocess.run(['gh','run','view',str(x['databaseId']),'--repo','d00rsfan/BiglyBTC','--log'],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=90)
assert all(x['conclusion']=='success' for x in data),data
print('Both exact-source hosted workflows passed; logs retained',flush=True)
