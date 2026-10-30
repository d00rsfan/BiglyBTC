#!/usr/bin/env python3
"""Root-reviewed two-test adoption and one javac for callback-order fixtures."""
import copy, hashlib, importlib.util, json, os, pathlib, shutil, subprocess
R=pathlib.Path('/home/yurii/Java/BiglyBT'); N=R.parent/'BiglyBTC'; W=pathlib.Path(__file__).resolve().parent
A=R/'build/manager-known-followup-20261029/affected-followup'
D=R/'build/manager-known-followup-20261029/diagnostics'
def sha(p): return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def load(p): return json.loads(pathlib.Path(p).read_text())
def save(p,v):
    with p.open('x') as f: f.write(json.dumps(v,indent=2)+'\n')
def git(*args): return subprocess.check_output(['git','-C',str(N),*args],text=True).strip()
def protected(): return {'head':subprocess.check_output(['git','-C',str(R),'rev-parse','HEAD'],text=True).strip(),'index_sha256':sha(R/'.git/index')}
def main():
    capfile=W/'manifest.json'; assert sha(capfile)=='9496e441043d6b763dda50d3df6e95bde5b2374a07d5834ee801da47c2f365b0'
    cap=load(capfile)
    for name,pin in cap['artifacts_sha256'].items(): assert sha(W/name)==pin,name
    for name,pin in cap['inputs_sha256'].items(): assert sha(name)==pin,name
    assert git('branch','--show-current')=='main' and git('rev-parse','HEAD')==cap['native_basis'] and not git('status','--porcelain')
    before=protected(); assert before=={'head':'cb8c6cb9bb97916cbc7e80338a017200ffc3e71f','index_sha256':'ac45601f3bed2b3a3d2ac91d715e4b052108a5b7478de62bb67f165ce0de30d4'}
    assert (R/'migration/upstreams.lock.json').read_bytes()==(N/'migration/upstreams.lock.json').read_bytes()
    adoption=copy.deepcopy(load(A/'effective-adoption.json'))
    for name,row in adoption['source_paths'].items(): assert sha(N/name)==row['after_sha256']
    assert not (W/'root-adoption.json').exists() and not (W/'java-prepared-r1').exists()
    for name,row in cap['source_paths'].items():
        assert sha(N/name)==sha(row['before_path'])==row['before_sha256'] and sha(row['after_path'])==row['after_sha256']
    for name,row in cap['source_paths'].items(): (N/name).write_bytes(pathlib.Path(row['after_path']).read_bytes())
    subprocess.run(['git','-C',str(N),'add','--',*cap['source_paths']],check=True)
    subprocess.run(['git','-C',str(N),'diff','--cached','--check'],check=True)
    subprocess.run(['git','-C',str(N),'-c','user.name=Codex','-c','user.email=codex@localhost','-c','commit.gpgsign=false','commit','-q','-m','Allow bounded JVM startup in original I2P interop fixtures'],check=True)
    source=git('rev-parse','HEAD'); assert set(git('diff','--name-only',cap['native_basis'],source).splitlines())==set(cap['source_paths'])
    for name,row in cap['source_paths'].items(): adoption['source_paths'][name].update(after_path=row['after_path'],after_sha256=row['after_sha256'])
    adoption.update(status='ADOPTED_FIXTURE_ORDER_SOURCE_ONLY',source_commit=source,basis_commit=cap['native_basis'],fixture_manifest={'path':str(capfile),'sha256':sha(capfile)},parent_effective_adoption={'path':str(A/'effective-adoption.json'),'sha256':sha(A/'effective-adoption.json')},original_head_index_unchanged=before,production_changed=False,runtime_credit=False,stop_after_current_milestone=True)
    assert len(adoption['source_paths'])==25 and not git('status','--porcelain') and protected()==before
    save(W/'root-adoption.json',adoption)
    spec=importlib.util.spec_from_file_location('diag_base',D/'two-selectors/run.py'); d=importlib.util.module_from_spec(spec);spec.loader.exec_module(d); b,s=d.b,d.s
    snapshot=b.source_snapshot(source)
    reference=D/'java-prepared-r1/result.json'; old=load(reference)
    for name,pin in old['classes_sha256'].items(): assert sha(name)==pin
    for name,pin in old['inputs_sha256'].items(): assert sha(name)==pin
    javac=pathlib.Path(next(name for name in old['inputs_sha256'] if name.endswith('/bin/javac')))
    sources=[pathlib.Path(cap['java_sources'][name]['after_path']) for name in ('StreamingPeer.java','ManagerPeer.java')]
    out=W/'java-prepared-r1';out.mkdir(mode=0o700)
    for name in ('home','tmp','all','legacy','manager'): (out/name).mkdir(mode=0o700)
    env=os.environ.copy()
    for name in ('JAVA_TOOL_OPTIONS','_JAVA_OPTIONS','JDK_JAVA_OPTIONS','CLASSPATH'): env.pop(name,None)
    env.update(HOME=str(out/'home'),TMPDIR=str(out/'tmp'));runner=s.Runner(out,env)
    runner.run('javac',['nice','-n','5',javac,'-J-Xms16m','-J-Xmx128m','-J-XX:ActiveProcessorCount=1','-J-XX:+UseSerialGC','-J-Duser.home='+str(out/'home'),'-J-Djava.io.tmpdir='+str(out/'tmp'),'--release','17','-cp',b.REFERENCE_JAR,'-d',out/'all',*sources],timeout=180)
    expected={'StreamingPeer.class','StreamingPeer$Adapter.class','ManagerPeer.class','ManagerPeer$Adapter.class'}
    assert {str(p.relative_to(out/'all')) for p in (out/'all').rglob('*') if p.is_file()}==expected
    for name in expected: shutil.copyfile(out/'all'/name,out/('legacy' if name.startswith('StreamingPeer') else 'manager')/name)
    assert b.source_snapshot(source)==snapshot and protected()==before and not git('status','--porcelain')
    result={'status':'PREPARED_FIN_CALLBACK_FIXTURE_JAVA_ONLY','qualification':False,'subjects_executed':False,'javac_invocations':1,'java_program':old['java_program'],'legacy_classes':str(out/'legacy'),'manager_classes':str(out/'manager'),'source_commit':source,'fixture_manifest':{'path':str(capfile),'sha256':sha(capfile)},'parent_reference':{'path':str(reference),'sha256':sha(reference)},'inputs_sha256':{str(p):sha(p) for p in (javac,b.REFERENCE_JAR,*sources)},'classes_sha256':{str(p):sha(p) for name in ('legacy','manager') for p in (out/name).glob('*.class')},'commands_sha256':sha(out/'commands.json'),'javac_log_sha256':sha(out/'javac.log'),'original_head_index_unchanged':before,'runner_sha256':sha(__file__)}
    save(out/'result.json',result);print(result['status'],source,flush=True)
if __name__=='__main__': main()
