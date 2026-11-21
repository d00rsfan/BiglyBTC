"""Private serial milestone publisher: explicit staging, provenance, no live data.

Resume the same immutable spec after a partial failure. Source identity is read
once after its implementation commit and retained before writing evidence.
"""
from pathlib import Path
import json,re,subprocess,sys,shutil,hashlib

R=Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';W=R/'build/public-nat-20261114'
spec_path=Path(sys.argv[1]);spec=json.loads(spec_path.read_text());out=spec_path.parent
state_path=spec_path.with_suffix('.publish-state.json')
digest=hashlib.sha256(spec_path.read_bytes()).hexdigest()
state=json.loads(state_path.read_text()) if state_path.exists() else {'spec':digest}
assert state['spec']==digest,'Use a new spec for a different milestone'
def save():
 temporary=state_path.with_suffix('.saving');temporary.write_text(json.dumps(state,indent=2)+'\n');temporary.replace(state_path)
names=[check['name'] for check in spec['checks']]
assert len(names)==len(set(names)),'Qualification check identities must be unique'
groups=[]
for check in spec['checks']:
 data=(W/(check['name']+'.log')).read_text(errors='replace')
 if check.get('qt',True):
  totals=re.findall(r'Totals: (\d+) passed, (\d+) failed, (\d+) skipped',data)
  assert totals and all(f=='0' and s=='0' for p,f,s in totals),check['name']
  groups.append({'name':check['name'],'qt_passed':sum(int(p) for p,f,s in totals),'failed':0,'skipped':0})
 else:
  assert check['match'] in data,check['name'];groups.append({'name':check['name'],'accepted':check['match']})
ns={};p=R/'build/general-chat-rooms-20261109/publish-foundation.py'
exec(compile(p.read_text().split('for repo,files,msg in ')[0],str(p),'exec'),ns)
# Retain valid UTF-8 and undecodable fixture bytes without hiding the staged diff.
ns['run']=lambda repo,*args:subprocess.check_output(['git','-C',str(repo),*args]).decode('utf-8',errors='surrogateescape').strip()
def commit(files,message,log):
 subprocess.run(['git','-C',str(N),'add','--',*files],check=True)
 ns['scan'](N)
 subprocess.run(['git','-C',str(N),'diff','--cached','--check'],check=True)
 with (out/log).open('w') as stream:
  subprocess.run(['git','-C',str(N),'-c','user.name=Codex','-c','user.email=codex@localhost','commit','-m',message],stdout=stream,stderr=subprocess.STDOUT,check=True)
if 'source' not in state:
 commit(spec['files'],spec['subject'],spec_path.stem+'-source-commit.log')
 source,tree=subprocess.check_output(['git','-C',str(N),'log','-1','--format=%H%n%T'],text=True).splitlines()
 state.update(source=source,tree=tree);save()
source=state['source'];tree=state['tree'];ev=spec['evidence']
def expand(text):return text.replace('@SOURCE@',source).replace('@TREE@',tree).replace('@EVIDENCE@',ev)
review=expand(spec['review'])
qualification={**spec.get('qualification',{}),'native_commit':source,'native_tree':tree,'groups':groups}
for repo in (R,N):
 directory=repo/ev;directory.mkdir(parents=True,exist_ok=True)
 (directory/'REVIEW.md').write_text(review.rstrip()+'\n')
 (directory/'qualification.json').write_text(json.dumps(qualification,indent=2)+'\n')
 for task in spec['tasks']:
  p=repo/task;append=expand(spec['task_update'])
  if append not in p.read_text():p.write_text(p.read_text()+'\n'+append+'\n')
 p=repo/'MIGRATION_PLAN.md';append=expand(spec['plan_update'])
 if append not in p.read_text():p.write_text(p.read_text()+'\n'+append+'\n')
p=R/'migration/upstreams.lock.json';registry=json.loads(p.read_text())
import jsonschema
schema=json.loads((R/'migration/upstreams.schema.json').read_text())
validator=jsonschema.Draft202012Validator({'$ref':'#/$defs/component','$defs':schema['$defs']})
for update in spec['components']:
 component=next(c for c in registry['components'] if c['id']==update['id'])
 if not any(item['commit']==source for item in component['native_commits']):
  component['native_commits'].append({'repository':'https://github.com/d00rsfan/BiglyBTC.git','commit':source,'purpose':'tests' if update.get('purpose')=='validation' else update.get('purpose','implementation')})
 component['target_paths']=sorted(set(component['target_paths']+update.get('target_paths',[])))
 qualified=set(update.get('first_port_source_paths',[]))
 if qualified:
  for coverage in component['source_coverage']:
   if set(coverage['source_paths'])==qualified and coverage['first_ported_commit'] is None:
    coverage['first_ported_commit']=source

 for field,paths in [('review_records',[ev+'/REVIEW.md']),('evidence',[ev+'/REVIEW.md',ev+'/qualification.json'])]:
  for path in paths:
   if path not in component[field]:component[field].append(path)
 validator.validate(component)
p.write_text(json.dumps(registry,indent=2)+'\n');shutil.copy2(p,N/'migration/upstreams.lock.json')
if not state.get('evidence_committed'):
 files=[ev+'/REVIEW.md',ev+'/qualification.json',*spec['tasks'],'MIGRATION_PLAN.md','migration/upstreams.lock.json']
 commit(files,spec['evidence_subject'],spec_path.stem+'-evidence-commit.log')
 state['evidence_committed']=True;save()
if not state.get('pushed') and '--no-push' not in sys.argv[2:]:
 with (out/(spec_path.stem+'-push.log')).open('w') as stream:
  subprocess.run(['git','-C',str(N),'-c','credential.helper=','-c','credential.helper=!gh auth git-credential','push','origin','main'],stdout=stream,stderr=subprocess.STDOUT,check=True)
 state['pushed']=True;save()
print('SOURCE',source);print('PUSH main PASS' if state.get('pushed') else 'LOCAL COMMITS READY; push awaits explicit authorization')
