from pathlib import Path
import json,hashlib,shutil,subprocess
R=Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';T=R/'build/i2p-multi-20261030';E=R/'migration/evidence/20261030-i2p-multi-torrent';E.mkdir(parents=True,exist_ok=True)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def copy(src,rel):
 dst=E/rel;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,dst)
for lane in ['private','default','san']:
 for run in sorted((T/lane).glob('r*')):
  if not (run/'result.json').exists() and not (run/'root-cancellation.json').exists():continue
  for p in run.iterdir():
   if p.is_file() and p.suffix in ['.json','.log']:copy(p,'native/'+lane+'/'+run.name+'/'+p.name)
for run in sorted((T/'acceptance/runs').glob('*')):
 public=run/'public-artifacts.json'
 if not public.exists():continue
 for name,digest in json.loads(public.read_text()).items():
  p=Path(name);assert p.parent==run and sha(p)==digest,p;copy(p,'acceptance/'+run.name+'/'+p.name)
 copy(public,'acceptance/'+run.name+'/public-artifacts.json')
for p in (T/'header-timeout-parser-check').iterdir():
 if p.is_file() and p.suffix in ['.java','.json','.log']:copy(p,'recipes/header-timeout-parser-check/'+p.name)
for p in (T/'acceptance').glob('root-abort-*.json'):copy(p,'recipes/acceptance/'+p.name)
for name in ['run.py','RouterMultiTorrentTracker.java','source-pins.json','fixture-r16-ready.json','manifest.json','ADOPTION.txt','changes.patch']:
 if (T/'acceptance'/name).is_file():copy(T/'acceptance'/name,'recipes/acceptance/'+name)
for p in (T/'acceptance/amendments').rglob('*'):
 if p.is_file() and p.suffix in ['.json','.patch','.txt','.java'] and not any(x in p.parts for x in ['before','staging']):copy(p,'recipes/acceptance/'+p.relative_to(T/'acceptance').as_posix())
for stage in ['source-implementation','source-audit','queue-audit','shared-stream-capacity','queue-ready-fence','queue-plan-diagnostics','queue-real-check-fence']:
 for p in (T/stage).rglob('*'):
  if p.is_file() and p.suffix in ['.json','.md','.txt','.patch'] and 'staging' not in p.parts:copy(p,'source-reviews/'+stage+'/'+p.relative_to(T/stage).as_posix())
P=T/'provider'
for p in P.iterdir():
 if p.is_file() and p.suffix in ['.json','.py','.patch','.txt']:copy(p,'provider/'+p.name)
for stage in ['amendments','recipe','recipe-r2']:
 for p in (P/stage).rglob('*'):
  if p.is_file() and p.suffix in ['.json','.py','.patch','.txt'] and 'staging' not in p.parts:copy(p,'provider/'+p.relative_to(P).as_posix())
for p in (P/'candidate-r1').iterdir():
 if p.is_file() and p.suffix in ['.json','.txt']:copy(p,'provider/candidate-r1/'+p.name)
for run in (P/'candidate-r1/runs').glob('*'):
 for p in run.iterdir():
  if p.is_file() and p.suffix in ['.json','.log']:copy(p,'provider/candidate-r1/runs/'+run.name+'/'+p.name)
for name in ['README-preview-draft.txt','run-batch.py','launch-acceptance.py','stage-private.py','preservation-before.json','bundle.json','bundle-launch.json','bundle-launch.log','bundle-ldd.log','source-review-summary.md','add-close-diagnostics.py','add-target-diagnostics.py','final-preservation.json','hosted-ci.json','runtime-source-equivalence.json','finish-san.py','publish-qualified-source.py','finalise-docs.py','seal-publication.py','collect-evidence.py']:
 if (T/name).is_file():copy(T/name,'recipes/'+name)
commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=N,text=True).strip();tree=subprocess.check_output(['git','rev-parse','HEAD:preview/native'],cwd=N,text=True).strip();base='6b40b2bb175e5e665d4c1865a587fe7b34d4cfd3'
(E/'source-diff.patch').write_bytes(subprocess.check_output(['git','diff',base,commit,'--','preview/native'],cwd=N))
(E/'source.json').write_text(json.dumps({'commit':commit,'tree':tree,'repository':'https://github.com/d00rsfan/BiglyBTC.git','base':base,'upstream':{'BiglyBT':'cb8c6cb9bb97916cbc7e80338a017200ffc3e71f','azneti2phelper':'33d97eca67a9f332e3d4da7e7492ec54f4bec078','I2P':'9134f808337b401e8e53c73734c81fab04280c9d','libtorrent':'740a0b9aeabe00e762cc0efe4a0f27593db2550b'}},indent=2)+'\n')
print(json.dumps({'evidence':str(E),'files':sum(1 for p in E.rglob('*') if p.is_file()),'source':commit}))
