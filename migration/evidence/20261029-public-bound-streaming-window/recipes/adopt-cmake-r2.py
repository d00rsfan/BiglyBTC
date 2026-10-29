#!/usr/bin/env python3
"""Root-only one-line direct Core dependency; reuse identical prepared Java classes."""
import hashlib,json,pathlib,shutil,subprocess
R=pathlib.Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';W=pathlib.Path(__file__).resolve().parent
NAME='preview/native/services/CMakeLists.txt'
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def load(p):return json.loads(pathlib.Path(p).read_text())
def write(p,v):p.parent.mkdir(parents=True,exist_ok=True);p.write_text(json.dumps(v,indent=2)+'\n')
def git(*args):return subprocess.check_output(['git','-C',str(N),*args],text=True).strip()
def main():
 assert not (W/'root-adoption-r2.json').exists()
 parent_path=W/'root-adoption.json';parent=load(parent_path);assert git('rev-parse','HEAD')==parent['source_commit']=='049c92e931e279af1ab66f8ff8ddcdf70d1247b6' and not git('status','--porcelain')
 for name,row in parent['source_paths'].items():assert sha(N/name)==row['after_sha256']
 assert load(W/'runs/clang-r1/result.json')['status']=='FAIL'
 capdir=W/'corrections/udp-core-dependency';capdir.mkdir(parents=True,exist_ok=False);before=capdir/'before.cmake';after=capdir/'after.cmake';before.write_bytes((N/NAME).read_bytes())
 addition=b'\n# Public endpoint predicate is declared in Core; UDP includes it directly.\ntarget_link_libraries(biglybtc_azureus_owned_udp PRIVATE BiglyBTC::Core)\n';assert addition not in before.read_bytes();after.write_bytes(before.read_bytes()+addition)
 import difflib
 (capdir/'changes.patch').write_text(''.join(difflib.unified_diff(before.read_text().splitlines(True),after.read_text().splitlines(True),fromfile='a/'+NAME,tofile='b/'+NAME)))
 history={str(p):sha(p) for p in (W/'runs/clang-r1').iterdir() if p.is_file() and p.suffix in ('.json','.log')}
 manifest={'status':'ROOT_REVIEWED_DIRECT_DEPENDENCY_CORRECTION','parent':{'path':str(parent_path),'sha256':sha(parent_path)},'source_paths':{NAME:{'before_path':str(before),'before_sha256':sha(before),'after_path':str(after),'after_sha256':sha(after)}},'artifacts_sha256':{p.name:sha(p) for p in (before,after,capdir/'changes.patch')},'failed_first_sha256':history,'scope':'Direct private Core dependency for its new header; no code/test/Java/tool/deadline changes','runtime_executed':False}
 cap=capdir/'manifest.json';write(cap,manifest)
 reference_path=W/'java-runs/r1/result.json';reference=load(reference_path);assert reference['status']=='PASS_PREPARED_STREAMING_WINDOW_HELPER' and reference['source_commit']==parent['source_commit']
 for group in ('java','javac','jar'):
  for file,pin in reference[group].items():assert sha(file)==pin['sha256'] and pathlib.Path(file).stat().st_size==pin['bytes']
 assert sha(reference['helper_source'])==reference['helper_source_sha256']
 assert {str(p.relative_to(reference['classes_directory'])):sha(p) for p in pathlib.Path(reference['classes_directory']).rglob('*.class')}==reference['classes']
 (N/NAME).write_bytes(after.read_bytes());git('add','--',NAME);git('diff','--cached','--check');git('-c','user.name=Codex','-c','user.email=codex@localhost','-c','commit.gpgsign=false','commit','-q','-m','Declare direct Core dependency for Azureus UDP endpoint policy')
 source=git('rev-parse','HEAD');assert not git('status','--porcelain')
 adoption=dict(parent);adoption.update(source_commit=source,previous_adoption={'path':str(parent_path),'sha256':sha(parent_path)},correction_capsules=[{'path':str(cap),'sha256':sha(cap)}],failed_first_sha256=history,adopter_sha256=sha(__file__))
 adoption['source_paths'][NAME]={**parent['source_paths'][NAME],'after_path':str(after),'after_sha256':sha(after)}
 adoption_path=W/'root-adoption-r2.json';write(adoption_path,adoption)
 out=W/'java-runs/r2';out.mkdir(exist_ok=False)
 for name in ('commands.json','java-version.log','javac-version.log','javac.log','inputs.json'):shutil.copyfile(reference_path.parent/name,out/name)
 reference.update(source_commit=source,adoption_sha256=sha(adoption_path),parent_reference={'path':str(reference_path),'sha256':sha(reference_path)},original_compile_source_commit=parent['source_commit'],original_compile_adoption_sha256=sha(parent_path),javac_reexecuted=False,reuse_reason='Only native UDP direct CMake dependency changed; actual prepared helper/JAR/tool/class bytes unchanged')
 write(out/'result.json',reference)
 print('ADOPTED',source,'26 paths; Java classes reused without javac')
if __name__=='__main__':main()
