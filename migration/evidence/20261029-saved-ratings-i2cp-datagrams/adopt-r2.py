#!/usr/bin/env python3
"""Apply the three reviewed correction leaves as one batch; preserve failed r1."""
import hashlib,json,pathlib,subprocess
R=pathlib.Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';K=pathlib.Path(__file__).resolve().parent
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def read(p):return json.loads(pathlib.Path(p).read_text())
def git(*args):return subprocess.check_output(['git','-C',str(N),*args],text=True).strip()
assert not (K/'root-adoption-r2.json').exists()
old=read(K/'root-adoption.json');assert git('rev-parse','HEAD')==old['source_commit']
assert not git('status','--porcelain')
amendments=[(R/'build/i2cp-datagram-submit-20261029/amendments/test-mutation-bounds/manifest.json','1481a13c1eec3678b2b25114e6e31cc3c76d448cedd75c6da104e4568fcfe845'),(K/'amendments/multivalue-publication-readback/manifest.json','96148caacce732f7fae0c8a77185d9e6c6a1afac9d42a06739aef4c3ef41709b')]
reviews=[(K/'datagram-bounds-source-review.json','5c4201cb3883425db6804d73be36dc1fc9f1072a78fe2058970fe5eb5b4c18c9'),(K/'multivalue-readback-source-review.json','5534942e5ffdf574aa2c3176603b823cdb1c7fc21dd8e4037d8719b639e7fb21')]
changes={}
for p,pin in amendments:
 assert sha(p)==pin
 d=read(p)
 if 'source_paths' in d:
  for name,row in d['source_paths'].items():changes[name]=(pathlib.Path(row['after_path']),row['before_sha256'],row['after_sha256'])
 else:
  for name,row in d['source_sha256'].items():changes[name]=(p.parent/'staging'/name,row['before'],row['after_sha256'])
for p,pin in reviews:assert sha(p)==pin
assert len(changes)==3
for name,(src,before,after) in changes.items():assert sha(N/name)==before and sha(src)==after and old['source_paths'][name]['after_sha256']==before
for name,(src,before,after) in changes.items():(N/name).write_bytes(src.read_bytes())
subprocess.run(['git','-C',str(N),'diff','--check'],check=True,stdout=subprocess.DEVNULL)
subprocess.run(['git','-C',str(N),'add','--',*changes],check=True)
subprocess.run(['git','-C',str(N),'-c','user.name=Codex','-c','user.email=codex@localhost','-c','commit.gpgsign=false','commit','-q','-m','Handle bounded multivalue DHT publication readback'],check=True)
d=old.copy();d['source_commit']=git('rev-parse','HEAD');d['initial_source_commit']=old['source_commit'];d['initial_adoption']={'path':str(K/'root-adoption.json'),'sha256':sha(K/'root-adoption.json')}
d['correction_capsules']=[{'path':str(p),'sha256':pin} for p,pin in amendments];d['correction_reviews']=[{'path':str(p),'sha256':pin} for p,pin in reviews];d['failed_r1_history']={'path':str(K/'failed-r1-history.json'),'sha256':sha(K/'failed-r1-history.json')}
for name,(src,before,after) in changes.items():d['source_paths'][name].update(after_path=str(src),after_sha256=after)
for name,row in d['source_paths'].items():assert sha(N/name)==row['after_sha256']
(K/'root-adoption-r2.json').write_text(json.dumps(d,indent=2)+'\n')
for name in ('run','private','offline'):
 code=(K/(name+'.py')).read_text().replace("H/'root-adoption.json'","H/'root-adoption-r2.json'").replace("H/'run.py'","H/'run-r2.py'")
 if name=='run':code=code.replace('Focused normal receiver/LeaseSet2 batch','Focused saved-rating/outbound-datagram batch')
 if name=='private':code=code.replace('seven normal receiver/LeaseSet2 suites','seven saved-rating/outbound-datagram suites')
 (K/(name+'-r2.py')).write_text(code)
print('ADOPTED',d['source_commit'],'paths',d['source_path_count'],'adoption',sha(K/'root-adoption-r2.json'))
