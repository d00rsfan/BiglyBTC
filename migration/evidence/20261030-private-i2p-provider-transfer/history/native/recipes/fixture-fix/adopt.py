"""Read-only source guard and reuse of the actual failed r2 Clang cache."""
import importlib.util,pathlib,subprocess,xml.etree.ElementTree as ET
W=pathlib.Path(__file__).resolve().parent;PREVIOUS_RECIPE=W.parent/'public-lt-headers'
spec=importlib.util.spec_from_file_location('previous_adoption',PREVIOUS_RECIPE/'adopt.py');p=importlib.util.module_from_spec(spec);spec.loader.exec_module(p)
PARENT=p.PARENT;R=p.R;N=p.N;BINDING=p.BINDING;sha=p.sha;load=p.load;write=p.write;ref=p.ref;protected=p.protected;shipping=p.shipping;snapshot=p.snapshot
SOURCE='8e9e248eea56f345f1bef169ea0bfcf7184cc7c3';PREVIOUS=p.SOURCE

def pins():
 p.pins();cap=load(W/'manifest.json')
 for name,pin in cap['artifacts_sha256'].items():assert sha(W/name)==pin,name
 for name,pin in cap['inputs_sha256'].items():assert sha(name)==pin,name
 plan=load(W/'source-plan.json')
 for name,row in plan['source_paths'].items():assert sha(row['after_path'])==row['after_sha256'],name
 return plan

def source_guard(source,adoption):
 plan=pins();fix=load(PARENT/'root-fixture-layout-fix.json');assert source==SOURCE==adoption['source_commit'] and adoption==load(W/'root-adoption.json')
 assert fix['parent']==PREVIOUS and adoption['parent_adoption']==ref(PREVIOUS_RECIPE/'root-adoption.json') and adoption['actual_root_fix']==ref(PARENT/'root-fixture-layout-fix.json') and adoption['source_plan']==ref(W/'source-plan.json')
 assert p.p.git('show','-s','--format=%P',source)==PREVIOUS and p.p.git('show','-s','--format=%P',PREVIOUS)==p.PREVIOUS
 assert p.p.git('diff','--name-only',PREVIOUS,source).splitlines()==[fix['path']] and set(p.p.git('diff','--name-only',p.p.BASIS,source).splitlines())==set(plan['source_paths'])
 before=subprocess.check_output(['git','-C',str(N),'show',PREVIOUS+':'+fix['path']]);assert __import__('hashlib').sha256(before).hexdigest()==fix['before_sha256'] and (W/'before'/fix['path']).read_bytes()==before
 for name,row in plan['source_paths'].items():assert sha(N/name)==row['after_sha256'],name
 previous=load(PREVIOUS_RECIPE/'runs/clang-r2/source-before.json');current=snapshot(source);expected=dict(previous['files_sha256']);expected[fix['path']]=fix['after_sha256'];assert current['files_sha256']==expected
 return current

def resume_cache(cache,approved,current):
 origin=PREVIOUS_RECIPE/'runs/clang-r2';failed=load(origin/'result.json');assert failed['status']=='FAIL' and failed['lane']=='clang' and failed['source_commit']==PREVIOUS and failed['failure']=="AssertionError('tests')" and failed['provider']==approved
 for name,pin in failed['artifacts_sha256'].items():assert sha(origin/name)==pin,name
 commands=load(origin/'commands.json');assert [x['name'] for x in commands]==['configure','build','inventory','tests'] and [x['exit'] for x in commands]==[0,0,0,8] and sha(origin/'commands.json')==failed['commands_sha256']
 for row in commands:assert sha(row['log'])==row['log_sha256'] and 'exception' not in row
 cases=list(ET.parse(origin/'tests.xml').getroot().iter('testcase'));bad=[x for x in cases if x.get('status')!='run' or any(x.find(t) is not None for t in ('failure','error','skipped'))];assert len(cases)==11 and len({x.get('name') for x in cases})==11 and len(bad)==1 and bad[0].get('name')=='owned-i2p-profile-source'
 for name,pin in load(origin/'tool-inputs.json').items():assert sha(name)==pin['sha256']
 for filename in ('dependencies-before.json','archives-before.json'):
  for name,pin in load(origin/filename).items():assert sha(name)==(pin['sha256'] if isinstance(pin,dict) else pin)
 for pin in load(origin/'images-before.json').values():assert sha(pin['path'])==pin['sha256'] and pathlib.Path(pin['path']).stat().st_size==pin['bytes']
 witness=load(W/'clang-cache-continuity.json');assert cache==PARENT/'caches/clang' and str(cache)==witness['cache'] and cache.is_dir() and not cache.is_symlink()
 assert witness['failed_result']==ref(origin/'result.json') and {str(q.relative_to(cache)):sha(q) for q in cache.rglob('*') if q.is_file()}==witness['files_sha256']
 assert load(origin/'source-before.json')['source_commit']==PREVIOUS and failed['adoption']==ref(PREVIOUS_RECIPE/'root-adoption.json')
