import pathlib,json,hashlib,subprocess
R=pathlib.Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';L=pathlib.Path(__file__).resolve().parent;W=R/'build/i2cp-datagram-receive-20261029/amendments/control-test-scope-format'
sha=lambda p:hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
assert sha(W/'manifest.json')=='7c33e6c90e4c83476f343f1362eb5cd284fe9da8a5647db30d932bdbc358ce57'
a=json.loads((L/'root-adoption.json').read_text());name='preview/native/tests/i2cp_receive_control_tests.cpp';src=W/'staging'/name
assert subprocess.check_output(['git','-C',str(N),'rev-parse','HEAD'],text=True).strip()==a['source_commit']
assert not subprocess.check_output(['git','-C',str(N),'status','--porcelain'],text=True).strip()
assert sha(N/name)==a['source_paths'][name]['after_sha256']
assert (N/name).read_bytes().replace(b'  }require(::fsync',b'  }\n  require(::fsync',1)==src.read_bytes()
(N/name).write_bytes(src.read_bytes())
subprocess.run(['git','-C',str(N),'diff','--check'],check=True,stdout=subprocess.DEVNULL)
subprocess.run(['git','-C',str(N),'add','--',name],check=True)
subprocess.run(['git','-C',str(N),'-c','user.name=Codex','-c','user.email=codex@localhost','-c','commit.gpgsign=false','commit','-q','-m','Clarify receive-control fixture loop scope for Clang'],check=True)
a['initial_adoption']={'path':str(L/'root-adoption.json'),'sha256':sha(L/'root-adoption.json')};a['source_commit']=subprocess.check_output(['git','-C',str(N),'rev-parse','HEAD'],text=True).strip();a['source_paths'][name].update(after_path=str(src),after_sha256=sha(src));a['correction_capsules']=[{'path':str(W/'manifest.json'),'sha256':sha(W/'manifest.json')}];a['format_proof']='Exactly one newline between closing brace and unchanged fsync assertion; byte-for-byte relation checked before mutation.'
(L/'root-adoption-r2.json').write_text(json.dumps(a,indent=2)+'\n')
for stem in ('run','private','offline'):
 code=(L/(stem+'.py')).read_text().replace("H/'root-adoption.json'","H/'root-adoption-r2.json'").replace("H/'run.py'","H/'run-r2.py'");(L/(stem+'-r2.py')).write_text(code)
print('ADOPTED_FORMAT_ONLY',a['source_commit'],'adoption',sha(L/'root-adoption-r2.json'))
