#!/usr/bin/env python3
"""Root-only finite retained-original local-value/version field comparison."""
import argparse,hashlib,json,os,pathlib,re,shutil,subprocess,tempfile
H=pathlib.Path(__file__).resolve().parent;W=H.parent;N=pathlib.Path('/home/yurii/Java/BiglyBTC');C=pathlib.Path('/tmp/biglybtc-oracle/classes');J=pathlib.Path('/usr/lib/jvm/java-17-openjdk-amd64/bin');RET=N/'build/dependencies/java-oracle-17-20261021/retention.json'
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def load(p):return json.loads(pathlib.Path(p).read_bytes())
def write(p,v):pathlib.Path(p).write_text(json.dumps(v,indent=2)+'\n')
def inventory(root):
 files=sorted(root.rglob('*'));assert not any(p.is_symlink() for p in files)
 return {str(p.relative_to(root)):sha(p) for p in files if p.is_file()}
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--driver',type=pathlib.Path,required=True);p.add_argument('--source-commit',required=True);p.add_argument('--build-receipt',type=pathlib.Path,required=True);p.add_argument('--build-source-map',type=pathlib.Path,required=True);p.add_argument('--attempt',required=True);a=p.parse_args();assert re.fullmatch('run-[A-Za-z0-9_-]+',a.attempt)
 manifest=load(W/'manifest.json')
 for rel,pin in manifest['artifacts'].items():assert sha(W/rel)==pin,rel
 assert sha(RET)=='7472e8bda8d832fd5a962cda76ba1fcefaa0e6e9b4cad2621e0be97e6fbec9dd';ret=load(RET);assert ret['source_commit']=='cb8c6cb9bb97916cbc7e80338a017200ffc3e71f' and ret['recompiled'] is False and inventory(C)==ret['file_sha256']
 git=lambda *args:subprocess.check_output(['git',*args],cwd=N,text=True).strip()
 assert git('rev-parse','HEAD')==a.source_commit and not git('status','--porcelain','--untracked-files=no','--','preview/native') and not git('ls-files','--others','--exclude-standard','--','preview/native')
 current={name:sha(N/name) for name in git('ls-files','--','preview/native').splitlines()};built=load(a.build_source_map);receipt=load(a.build_receipt)
 assert built==current and receipt['exit']==0 and receipt['source_manifest_sha256']==sha(a.build_source_map)
 for name,row in manifest['source_paths'].items():assert current[name]==row['after_sha256']
 command=receipt['command'];assert '--build' in command and '--target' in command and 'biglybtc-azureus-database-runtime-tests' in command
 assert a.driver.resolve()==(pathlib.Path(command[command.index('--build')+1])/'tests/biglybtc-azureus-database-runtime-tests').resolve();driver_sha=sha(a.driver)
 out=H/'runs'/a.attempt;out.parent.mkdir(exist_ok=True);assert not out.exists();work=pathlib.Path(tempfile.mkdtemp(prefix='biglybtc-queue-oracle-',dir='/tmp'))
 for name in ('classes','empty-sourcepath','tmp'):(work/name).mkdir()
 env={k:v for k,v in os.environ.items() if k not in ('CLASSPATH','JAVA_TOOL_OPTIONS','_JAVA_OPTIONS','JDK_JAVA_OPTIONS','LD_PRELOAD','LD_AUDIT')};commands=[];result={'status':'FAIL','source_commit':a.source_commit,'original_commit':ret['source_commit'],'native_driver_sha256':driver_sha,'source_manifest_sha256':sha(a.build_source_map),'build_receipt_sha256':sha(a.build_receipt),'original_recompiled':False,'profile_or_network_interoperability_claimed':False,'persistence_callback':'explicit-noop-adapter','contact':'explicit-fixed-field-adapter'}
 def run(name,args):
  row={'name':name,'command':[str(x) for x in args]};commands.append(row);write(work/'commands.json',commands)
  with (work/(name+'.stdout')).open('wb') as stdout,(work/(name+'.stderr')).open('wb') as stderr:
   child=subprocess.Popen(row['command'],cwd=work,env=env,stdin=subprocess.DEVNULL,stdout=stdout,stderr=stderr)
   try:row['exit']=child.wait(timeout=45)
   except subprocess.TimeoutExpired:child.kill();child.wait();raise
  write(work/'commands.json',commands);assert row['exit']==0,name
 try:
  run('javac',[J/'javac','-J-XX:ActiveProcessorCount=2','--release','8','-encoding','UTF-8','-proc:none','-implicit:none','-sourcepath',work/'empty-sourcepath','-cp',C,'-d',work/'classes',*sorted((H/'source').glob('*.java'))])
  run('original',[J/'java','-XX:ActiveProcessorCount=2','-Xmx128m','-Djava.security.manager=allow','-Dqueue.oracle.root='+str(work),'-Dqueue.oracle.cohort='+str(C),'-Djava.io.tmpdir='+str(work/'tmp'),'-Duser.home='+str(work/'forbidden-home'),'-Dazureus.config.path='+str(work/'forbidden-profile'),'-cp',work/'classes','LocalPublishLauncher',work/'classes',C])
  run('native',[a.driver.resolve(),'--project-original-local-values'])
  original=(work/'original.stdout').read_bytes();native=(work/'native.stdout').read_bytes();assert original==native and len(original.splitlines())==4 and original.startswith(b'V\t0\t2100000000\t2100000128\n'), 'Actual mismatch preserved'
  assert sha(a.driver)==driver_sha and inventory(C)==ret['file_sha256'] and {name:sha(N/name) for name in current}==current
  result.update(status='PASS_RETAINED_ORIGINAL_LOCAL_VALUE_VERSION_FIELDS',actual_version_cases=3,actual_value_cases=1,cohort_unchanged=True,native_unchanged=True)
 except BaseException as failure:result['failure']=repr(failure)
 finally:
  write(work/'result.json',result);shutil.copytree(work,out);shutil.rmtree(work)
 print(result['status'],out/'result.json');return 0 if result['status'].startswith('PASS_') else 1
if __name__=='__main__':raise SystemExit(main())
