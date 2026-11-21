"""Install exactly the qualified pending-controller implementation and tests."""
from pathlib import Path
import json,shutil,subprocess,hashlib
D=Path(__file__).resolve().parent;N=Path('/home/yurii/Java/BiglyBTC');P=json.loads((D/'pre-implementation.json').read_text());S=D/'native-staged';paths=[]
assert subprocess.check_output(['git','-C',str(N),'rev-parse','HEAD'],text=True).strip()==P['native_parent']
dirty=subprocess.check_output(['git','-C',str(N),'status','--porcelain'],text=True).rstrip('\n')
if dirty:
 previous=json.loads((D/'verification-inputs.json').read_text())
 assert all(hashlib.sha256((N/name).read_bytes()).hexdigest()==value for name,value in previous.items()),'Native source changed outside this staged slice'
 assert set(line[3:] for line in dirty.splitlines())<=set(previous)
def stage(path,data):
 target=S/path;target.parent.mkdir(parents=True,exist_ok=True)
 if not target.exists() or target.read_bytes()!=data:target.write_bytes(data)
 paths.append(path)
for name in ['profile_tag_rates.hpp','profile_tag_rates.cpp','profile_prepared_tag_attachment.hpp']:
 stage('preview/native/engine/'+name,(D/'drafts'/name).read_bytes())
stage('preview/native/tests/profile_tag_preadd_tests.cpp',(D/'profile_preadd_tests.cpp').read_bytes())
cmake=subprocess.check_output(['git','-C',str(N),'show',P['native_parent']+':preview/native/tests/CMakeLists.txt'],text=True)
cmake+='''
if(TARGET biglybtc_local_engine)
 add_executable(biglybtc-profile-tag-preadd-tests profile_tag_preadd_tests.cpp)
 target_link_libraries(biglybtc-profile-tag-preadd-tests PRIVATE biglybtc_local_engine LibtorrentRasterbar::torrent-rasterbar Threads::Threads Qt6::Core)
 target_compile_options(biglybtc-profile-tag-preadd-tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
 foreach(mode bound empty stale close damaged foreign cohort worker sdk-abort no-lease held-gate abandoned abandoned-installed)
  add_test(NAME actual-profile-tag-preadd-${mode} COMMAND biglybtc-profile-tag-preadd-tests ${mode})
  set_tests_properties(actual-profile-tag-preadd-${mode} PROPERTIES TIMEOUT 90)
 endforeach()
endif()
'''
stage('preview/native/tests/CMakeLists.txt',cmake.encode())
for name in paths:
 target=N/name;target.parent.mkdir(parents=True,exist_ok=True)
 if not target.exists() or target.read_bytes()!=(S/name).read_bytes():shutil.copy2(S/name,target)
(D/'native-source-paths.json').write_text(json.dumps(paths,indent=2)+'\n')
(D/'verification-inputs.json').write_text(json.dumps({name:hashlib.sha256((N/name).read_bytes()).hexdigest() for name in paths},indent=2)+'\n')
print('Installed',len(paths),'exact Native paths;private storage/rate fence unchanged')
