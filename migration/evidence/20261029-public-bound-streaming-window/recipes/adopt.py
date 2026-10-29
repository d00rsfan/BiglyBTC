#!/usr/bin/env python3
"""Root-only adoption of reviewed PublicBound/window overlays, no import actions."""
import hashlib,json,pathlib,subprocess
R=pathlib.Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';W=pathlib.Path(__file__).resolve().parent
P=R/'build/azureus-public-bound-20261029/amendments/final-send-authority/manifest.json'
V=R/'build/i2p-streaming-window-20261029/implementation/amendments/first-ack-packet-age/amendments/ack-before-rtt-timer/manifest.json'
A=R/'build/i2p-streaming-window-20261029/acceptance/manifest.json'
CAPS={str(P):'122e845c0bbdd2edc499932d260b807df3c6c9c52a37251b885042c2943fde3a',str(V):'6e6fe54d99eb26a190a53307c1d49527aeda9c6ec6217a6a73b603dd942243f1',str(A):'fec3012f2abcda6ba8be92ff9ea767bbd291f1dd52019f8cb8ef441554c896dc'}
REVIEWS={str(R/'build/azureus-public-bound-20261029-review/review.json'):'803384933d6ee11eee1949e3c11ca3c912178fd074319adbf39567043fd10adb',str(R/'build/i2p-streaming-window-20261029-review/review.json'):'49b742d56e3623d351bb5ef72c023f5159e91b90a49e600a1c5ad7d565a8a5ea',str(R/'build/i2p-streaming-window-20261029-acceptance-review/review.json'):'d471b7cbbae292729e0f1228c25025698db9d2b64c3d383cddcc660866e4653e'}
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def load(p):return json.loads(pathlib.Path(p).read_text())
def git(*args):return subprocess.check_output(['git','-C',str(N),*args],text=True).strip()
def main():
 assert not (W/'root-adoption.json').exists(),'One-time adopter already consumed'
 assert git('rev-parse','HEAD')=='f1973f944e9640fe0eb2b3b0bb42557c3324bed3' and git('branch','--show-current')=='main' and not git('status','--porcelain')
 for path,pin in {**CAPS,**REVIEWS}.items():assert sha(path)==pin,path
 assert sha(W/'manifest.json')=='a650175bab2d67a57c79c6196d0d37c15c8db5418fa05a10f1e60d7f30c61c4a'
 for name,pin in load(W/'manifest.json')['artifacts_sha256'].items():assert sha(W/name)==pin
 caps=[load(p) for p in (P,V,A)];changes={};maps={};seen=set()
 for path,cap in zip((P,V,A),caps,strict=True):
  for name,pin in cap['artifacts_sha256'].items():assert sha(path.parent/name)==pin,(path,name)
  for name,row in cap['source_paths'].items():
   if name.endswith('/CMakeLists.txt'):continue
   assert name.startswith('preview/native/') and '..' not in pathlib.Path(name).parts and name not in seen;seen.add(name)
   dest=N/name;before=sha(dest) if dest.exists() else None;assert before==row['before_sha256'],name
   assert sha(row['after_path'])==row['after_sha256'],name
   changes[name]=pathlib.Path(row['after_path']).read_bytes();maps[name]=dict(row)
 # Apply accepted selector delta on its exact current basis before appending
 # PublicBound test registration. Preserve any independently appended suffix.
 cmake='preview/native/tests/CMakeLists.txt';row=caps[2]['source_paths'][cmake]
 before=pathlib.Path(row['before_path']).read_bytes();after=pathlib.Path(row['after_path']).read_bytes();current=(N/cmake).read_bytes()
 assert sha(row['before_path'])==row['before_sha256'] and sha(row['after_path'])==row['after_sha256'] and current.startswith(before)
 changes[cmake]=after+current[len(before):]
 assert sha(N/caps[1]['cmake_append']['path'])==caps[1]['cmake_append']['basis_sha256']
 fragments=caps[0]['cmake_fragments']+[{'destination':caps[1]['cmake_append']['path'],'path':caps[1]['cmake_append']['append_path'],'sha256':caps[1]['cmake_append']['append_sha256']}]
 for fragment in fragments:
  name=fragment['destination'];assert name in ('preview/native/adapters/CMakeLists.txt','preview/native/services/CMakeLists.txt','preview/native/tests/CMakeLists.txt')
  assert sha(fragment['path'])==fragment['sha256'];raw=pathlib.Path(fragment['path']).read_bytes();current=changes.get(name,(N/name).read_bytes());assert raw not in current
  changes[name]=current+b'\n'+raw
 for name in changes:
  if name.endswith('/CMakeLists.txt'):
   old=W/'before'/name;new=W/'staging'/name;old.parent.mkdir(parents=True,exist_ok=True);new.parent.mkdir(parents=True,exist_ok=True);assert not old.exists() and not new.exists();old.write_bytes((N/name).read_bytes());new.write_bytes(changes[name]);maps[name]={'before_path':str(old),'before_sha256':sha(old),'after_path':str(new),'after_sha256':sha(new)}
 assert len(maps)==len(changes)==26
 for name,row in maps.items():assert (sha(N/name) if (N/name).exists() else None)==row['before_sha256']
 for name,raw in changes.items():dest=N/name;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(raw)
 for name,row in maps.items():assert sha(N/name)==row['after_sha256']
 git('add','--',*maps);git('diff','--cached','--check')
 git('-c','user.name=Codex','-c','user.email=codex@localhost','-c','commit.gpgsign=false','commit','-q','-m','Add explicit PublicBound and bounded Streaming window')
 source=git('rev-parse','HEAD');assert not git('status','--porcelain')
 result={'status':'ADOPTED_REVIEWED_SOURCE_ONLY','source_commit':source,'previous_native_commit':'f1973f944e9640fe0eb2b3b0bb42557c3324bed3','source_paths':maps,'source_capsules':[{'path':p,'sha256':v} for p,v in CAPS.items()],'source_reviews':[{'path':p,'sha256':v} for p,v in REVIEWS.items()],'acceptance_recipe':{'path':str(W/'manifest.json'),'sha256':sha(W/'manifest.json')},'cmake_fragments':fragments,'adopter_sha256':sha(__file__),'runtime_executed':False}
 (W/'root-adoption.json').write_text(json.dumps(result,indent=2)+'\n');print('ADOPTED',source,len(maps))
if __name__=='__main__':main()
