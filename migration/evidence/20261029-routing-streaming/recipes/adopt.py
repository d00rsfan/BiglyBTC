import hashlib,json,pathlib,subprocess
R=pathlib.Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';H=pathlib.Path(__file__).resolve().parent
PARENT='cc5dada94a361685a5254edfac24de9988764d5c'
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def load(p):return json.loads(pathlib.Path(p).read_text())
def git(*args):return subprocess.check_output(['git','-C',str(N),*args],text=True).strip()
refs=[(R/'build/azureus-routing-bootstrap-20261029/amendments/actor-member-order/manifest.json','b85be109bd0f676671083c5efc1d3eb51f3c81edd9d4b3bbf85f9cf90c51cf9f',R/'build/azureus-routing-bootstrap-20261029-review/review.json','6ca639726bd4d8cb23c5a19b78e5296cf015927de611cfc84d43521ee8b62a1d'),(R/'build/i2p-streaming-20261029/manifest.json','757cfa578b67533591a3dbc6880345d651708d8e0b79b25531458def13d216b7',R/'build/i2p-streaming-20261029-review/review.json','81243542132106ddfdb74c4336800af8f1d481d231b348fbcb17be17176e9001'),(R/'build/i2p-streaming-java-peer-20261029/amendments/trailing-peer-stderr/manifest.json','a77d16dc7ecb725d50d9529e1cb07fc41246a89a3192aa5958dfd0097d7e517c',R/'build/i2p-streaming-java-peer-20261029-review/review.json','4893c9016d7f2aa9d14fbec3f449d648a07a03820548432d905b322b76ffef33')]
assert not (H/'root-adoption.json').exists() and git('rev-parse','HEAD')==PARENT and not git('status','--porcelain')
assert sha(H/'manifest.json')=='b641cacf1816e67c142043997cfd130c17769cc85ebad610f76acaae4c2aedc2'
for name,pin in load(H/'manifest.json').get('artifacts_sha256',load(H/'manifest.json').get('artifacts',{})).items():assert sha(H/name)==pin
seen={}
def verify(path,pin):
 path=pathlib.Path(path);assert sha(path)==pin
 if str(path) in seen:return seen[str(path)]
 d=load(path);seen[str(path)]=d
 for name,value in d.get('artifacts_sha256',d.get('artifacts',{})).items():assert sha(path.parent/name)==value,(path,name)
 for key in ('parent_manifest','parent'):
  if isinstance(d.get(key),dict):verify(d[key]['path'],d[key]['sha256'])
 for ref in d.get('effective_artifacts',{}).values():assert sha(ref['path'])==ref['sha256']
 return d
rows={};caps=[];reviews=[];maps=[]
for path,pin,review,rpin in refs:
 d=verify(path,pin);assert sha(review)==rpin;maps.append(d);caps.append({'path':str(path),'sha256':pin});reviews.append({'path':str(review),'sha256':rpin})
 for name,row in d['source_paths'].items():
  assert name.startswith('preview/native/') and '..' not in pathlib.PurePosixPath(name).parts and name not in rows
  assert (sha(N/name) if (N/name).exists() else None)==row['before_sha256'] and sha(row['after_path'])==row['after_sha256'],name
  if row['before_path']:assert sha(row['before_path'])==row['before_sha256']
  rows[name]=dict(row)
assert len(rows)==20
fragments={'preview/native/services/CMakeLists.txt':[],'preview/native/tests/CMakeLists.txt':[]}
for ref in maps[0]['cmake_append_fragments']:
 assert sha(ref['path'])==ref['sha256'];target='services' if pathlib.Path(ref['path']).name=='services.CMake.fragment' else 'tests';fragments['preview/native/'+target+'/CMakeLists.txt'].append(pathlib.Path(ref['path']).read_text())
stream=refs[1][0].parent/'CMake.fragment';split=stream.read_text().split('# Tests:',1);assert len(split)==2
fragments['preview/native/services/CMakeLists.txt'].append(split[0]);fragments['preview/native/tests/CMakeLists.txt'].append('# Tests:'+split[1])
ref=maps[2]['cmake_append'];assert sha(ref['path'])==ref['sha256'];fragments[ref['location'].split(';',1)[0]].append(pathlib.Path(ref['path']).read_text())
for name,parts in fragments.items():
 before=H/'before'/name;after=H/'composed'/name;before.parent.mkdir(parents=True,exist_ok=True);after.parent.mkdir(parents=True,exist_ok=True);before.write_bytes((N/name).read_bytes());after.write_text(before.read_text().rstrip()+'\n\n'+'\n\n'.join(p.strip() for p in parts)+'\n');rows[name]={'before_path':str(before),'before_sha256':sha(before),'after_path':str(after),'after_sha256':sha(after)}
assert len(rows)==22
for name,row in rows.items():
 p=N/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(pathlib.Path(row['after_path']).read_bytes())
subprocess.run(['git','-C',str(N),'add','--',*rows],check=True)
subprocess.run(['git','-C',str(N),'diff','--cached','--check'],check=True)
subprocess.run(['git','-C',str(N),'-c','user.name=Codex','-c','user.email=codex@localhost','-c','commit.gpgsign=false','commit','-q','-m','Add owned DHT bootstrap and reliable native I2P streams'],check=True)
a={'source_commit':git('rev-parse','HEAD'),'parent_commit':PARENT,'source_path_count':len(rows),'source_paths':rows,'source_capsules':caps,'source_reviews':reviews,'source_history':[{'path':p,'sha256':sha(p)} for p in seen if p not in {str(r[0]) for r in refs}],'adopter_sha256':sha(__file__),'recipe_manifest_sha256':sha(H/'manifest.json')};(H/'root-adoption.json').write_text(json.dumps(a,indent=2)+'\n');print('ADOPTED',a['source_commit'],'paths',len(rows))
