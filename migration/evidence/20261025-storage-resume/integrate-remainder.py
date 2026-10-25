from pathlib import Path
import hashlib,json,shutil,subprocess
w=Path(__file__).resolve().parent;c=w/'remainder/save-defaults';s=w/'engine/staging';n=Path('/home/yurii/Java/BiglyBTC');h=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();m=json.loads((c/'manifest.json').read_text());assert h(c/'manifest.json')=='afff5a253896934b309d2c5d96b2e28bbbc2b087fc32ef0e4277beed70935bb2'
for key in ['sources','before_sha256','patches','context']:
 for rel,sha in m.get(key,{}).items():
  p=(c/'staging'/rel) if key=='sources' else (c/'before'/rel) if key=='before_sha256' else c/rel
  assert h(p)==sha,(key,rel)
for rel,sha in m['before_sha256'].items():
 p=s/rel
 if not p.exists():
  assert h(n/rel)==sha,rel;p.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(n/rel,p)
 assert h(p)==sha,rel
for cap in [c,w/'remainder/negative-links']:
 for patch in ['managed_storage.patch','file_operations.patch']:
  for dry in [True,False]:
   cmd=['patch','--batch','--forward','-p1','-i',str(cap/patch)];cmd+=['--dry-run'] if dry else []
   r=subprocess.run(cmd,cwd=s,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True);assert r.returncode==0,r.stdout
for rel in m['new_paths']+[r for r in m['before_sha256'] if '/tests/' in r]:
 p=s/rel;p.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(c/'staging'/rel,p)
p=s/'preview/native/adapters/CMakeLists.txt';t=p.read_text();assert 'original_storage_defaults.cpp' not in t;p.write_text(t+'\ntarget_sources(biglybtc_profile PRIVATE original_storage_defaults.cpp)\n')
p=s/'preview/native/tests/CMakeLists.txt';t=p.read_text();assert 'biglybtc-original-storage-defaults-tests' not in t;p.write_text(t+'''
add_executable(biglybtc-original-storage-defaults-tests original_storage_defaults_tests.cpp)
target_link_libraries(biglybtc-original-storage-defaults-tests PRIVATE biglybtc_profile)
target_compile_options(biglybtc-original-storage-defaults-tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME original-storage-defaults COMMAND biglybtc-original-storage-defaults-tests)
set_tests_properties(original-storage-defaults PROPERTIES TIMEOUT 120)
''')
p=s/'preview/native/tests/managed_storage_tests.cpp';t=p.read_text();assert '  test("negative-original-links' not in t;pos=t.index('  test(',t.index('int main'));t=t[:pos]+(w/'remainder/negative-links/managed-storage-tests.inc').read_text()+'\n'+t[pos:];p.write_text(t)
p=s/'preview/native/tests/file_operations_tests.cpp';t=p.read_text();marker='void authenticatedExternalLinks(){';assert t.count(marker)==1;t=t.replace(marker,marker+'\n'+(w/'remainder/negative-links/file-operations-tests.inc').read_text(),1);p.write_text(t)
print('Composed verified save-default and negative-link source/tests/CMake; not adopted')
