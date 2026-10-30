"""Read-only source/cached-failure association; root already applied the one-line fix."""
import importlib.util,pathlib,subprocess
W=pathlib.Path(__file__).resolve().parent;PARENT=W.parent.parent
spec=importlib.util.spec_from_file_location('parent_adoption',PARENT/'adopt.py');p=importlib.util.module_from_spec(spec);spec.loader.exec_module(p)
R=p.R;N=p.N;BINDING=p.BINDING;sha=p.sha;load=p.load;write=p.write;ref=p.ref;protected=p.protected;shipping=p.shipping;snapshot=p.snapshot
SOURCE='af1aef1b44cdce4423cef9f37125edae21794a7e';PREVIOUS='078cc91db1aff4195986beb860063aad3f8b6967'
def pins():
 p.pins();cap=load(W/'manifest.json')
 for name,pin in cap['artifacts_sha256'].items():assert sha(W/name)==pin,name
 for name,pin in cap['inputs_sha256'].items():assert sha(name)==pin,name
 plan=load(W/'source-plan.json')
 for name,row in plan['source_paths'].items():assert sha(row['after_path'])==row['after_sha256'],name
 return plan

def source_guard(source,adoption):
 plan=pins();fix=load(PARENT/'root-header-dependency-fix.json');assert source==SOURCE==adoption['source_commit'] and adoption==load(W/'root-adoption.json')
 assert adoption['parent_adoption']==ref(PARENT/'root-adoption.json') and adoption['actual_root_fix']==ref(PARENT/'root-header-dependency-fix.json') and adoption['source_plan']==ref(W/'source-plan.json')
 assert p.git('show','-s','--format=%P',source)==PREVIOUS and p.git('show','-s','--format=%P',PREVIOUS)==p.BASIS
 assert p.git('diff','--name-only',PREVIOUS,source).splitlines()==[fix['path']] and set(p.git('diff','--name-only',p.BASIS,source).splitlines())==set(plan['source_paths'])
 before=subprocess.check_output(['git','-C',str(N),'show',PREVIOUS+':'+fix['path']]);assert __import__('hashlib').sha256(before).hexdigest()==fix['before_sha256']
 assert (W/'before'/fix['path']).read_bytes()==before and sha(N/fix['path'])==fix['after_sha256']
 for name,row in plan['source_paths'].items():assert sha(N/name)==row['after_sha256'],name
 previous=load(PARENT/'runs/clang-r1/source-before.json');current=snapshot(source);expected=dict(previous['files_sha256']);expected[fix['path']]=fix['after_sha256'];assert current['files_sha256']==expected
 return current

def resume_cache(cache,approved,current):
 origin=PARENT/'runs/clang-r1';failed=load(origin/'result.json');assert failed['status']=='FAIL' and failed['lane']=='clang' and failed['source_commit']==PREVIOUS and failed['failure']=="AssertionError('build')" and failed['provider']==approved
 for name,pin in failed['artifacts_sha256'].items():assert sha(origin/name)==pin,name
 commands=load(origin/'commands.json');assert [x['name'] for x in commands]==['configure','build'] and [x['exit'] for x in commands]==[0,1] and sha(origin/'commands.json')==failed['commands_sha256']
 for row in commands:assert sha(row['log'])==row['log_sha256'] and 'exception' not in row
 assert "'libtorrent/add_torrent_params.hpp' file not found" in (origin/'build.log').read_text()
 for name,pin in load(origin/'tool-inputs.json').items():assert sha(name)==pin['sha256']
 witness=load(W/'clang-cache-continuity.json');assert cache==PARENT/'caches/clang' and str(cache)==witness['cache'] and cache.is_dir() and not cache.is_symlink()
 assert witness['failed_result']==ref(origin/'result.json') and {str(p.relative_to(cache)):sha(p) for p in cache.rglob('*') if p.is_file()}==witness['files_sha256']
 assert load(origin/'source-before.json')['source_commit']==PREVIOUS and failed['adoption']==ref(PARENT/'root-adoption.json')
