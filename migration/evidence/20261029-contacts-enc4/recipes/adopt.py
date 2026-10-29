"""Adopt one reviewed union; approved-inputs.json pins actual frozen sources."""
import hashlib,json,pathlib,subprocess
R=pathlib.Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';H=pathlib.Path(__file__).resolve().parent
PARENT='2c79155e6c660426781ef54b4c075451e304a5b8'
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def load(p):return json.loads(pathlib.Path(p).read_text())
def git(*args):return subprocess.check_output(['git','-C',str(N),*args],text=True).strip()
def main():
 assert not (H/'root-adoption.json').exists() and git('rev-parse','HEAD')==PARENT and not git('status','--porcelain')
 plan=load(H/'approved-inputs.json');assert plan['parent_commit']==PARENT and plan['status']=='APPROVED_SOURCE_ONLY'
 recipe=load(H/'manifest.json')
 for name,pin in recipe['artifacts_sha256'].items():assert sha(H/name)==pin,name
 seen={}
 def verify(ref):
  p=pathlib.Path(ref['path']);assert sha(p)==ref['sha256']
  if str(p) in seen:return seen[str(p)]
  d=load(p);seen[str(p)]=d
  for name,pin in d.get('artifacts_sha256',d.get('artifacts',{})).items():
   assert sha(p.parent/name)==(pin['sha256'] if isinstance(pin,dict) else pin),(p,name)
   if isinstance(pin,dict):assert (p.parent/name).stat().st_size==pin['bytes']
  for key in ('parent','parent_manifest'):
   if isinstance(d.get(key),dict):verify(d[key])
  for row in d.get('effective_artifacts',{}).values():assert sha(row['path'])==row['sha256']
  return d
 rows={}
 for ref,review in zip(plan['source_capsules'],plan['source_reviews'],strict=True):
  d=verify(ref);assert sha(review['path'])==review['sha256']
  maps=d.get('source_paths')
  if maps is None:maps={row['path']:{'before_path':row['before'],'before_sha256':row['before_sha256'],'after_path':row['after'],'after_sha256':row['after_sha256']} for row in d['source_maps']}
  for name,row in maps.items():
   assert name.startswith('preview/native/') and '..' not in pathlib.PurePosixPath(name).parts and name not in rows
   assert (sha(N/name) if (N/name).exists() else None)==row['before_sha256'] and sha(row['after_path'])==row['after_sha256'],name
   if row['before_path']:assert sha(row['before_path'])==row['before_sha256']
   rows[name]=dict(row)
 for target,fragments in plan['cmake_append'].items():
  assert target.startswith('preview/native/') and target.endswith('/CMakeLists.txt') and target not in rows
  before=H/'before'/target;after=H/'composed'/target;before.parent.mkdir(parents=True,exist_ok=True);after.parent.mkdir(parents=True,exist_ok=True);before.write_bytes((N/target).read_bytes())
  parts=[]
  for ref in fragments:
   assert sha(ref['path'])==ref['sha256'] and sha(N/target)==ref['basis_sha256'];parts.append(pathlib.Path(ref['path']).read_text().strip())
  after.write_text(before.read_text().rstrip()+'\n\n'+'\n\n'.join(parts)+'\n');rows[target]={'before_path':str(before),'before_sha256':sha(before),'after_path':str(after),'after_sha256':sha(after)}
 assert len(rows)==plan['source_path_count']
 for name,row in rows.items():
  p=N/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(pathlib.Path(row['after_path']).read_bytes())
 subprocess.run(['git','-C',str(N),'add','--',*rows],check=True);subprocess.run(['git','-C',str(N),'diff','--cached','--check'],check=True)
 subprocess.run(['git','-C',str(N),'-c','user.name=Codex','-c','user.email=codex@localhost','-c','commit.gpgsign=false','commit','-q','-m','Add original contact persistence, routing maintenance and Enc4 lease keys'],check=True)
 receipt={'source_commit':git('rev-parse','HEAD'),'parent_commit':PARENT,'source_path_count':len(rows),'source_paths':rows,'source_capsules':plan['source_capsules'],'source_reviews':plan['source_reviews'],'source_history':[{'path':p,'sha256':sha(p)} for p in seen if p not in {r['path'] for r in plan['source_capsules']}],'adopter_sha256':sha(__file__),'recipe_manifest_sha256':sha(H/'manifest.json'),'approved_inputs_sha256':sha(H/'approved-inputs.json')}
 (H/'root-adoption.json').write_text(json.dumps(receipt,indent=2)+'\n');print('ADOPTED',receipt['source_commit'],len(rows))
if __name__=='__main__':main()
