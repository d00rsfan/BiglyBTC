"""Copy the qualified pre-add implementation and self-contained Native controls."""
from pathlib import Path
import json,shutil,subprocess
D=Path(__file__).resolve().parent;N=Path('/home/yurii/Java/BiglyBTC');S=D/'native-staged';parent=json.loads((D/'pre-implementation.json').read_text())['native_parent']
assert subprocess.check_output(['git','-C',str(N),'rev-parse','HEAD'],text=True).strip()==parent
paths=[]
def stage(path,data):
 p=S/path;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data);paths.append(path)
for name in ['sdk_tag_rate_group.cpp','sdk_tag_rate_group.hpp','sdk_prepared_tag_attachment.hpp']:
 stage('preview/native/engine/'+name,(D/'drafts'/name).read_bytes())
fixture=D.parent/'tag-rate-cross-owner-20261121/transport_fixture.hpp'
stage('preview/native/tests/tag_preadd_fixture.hpp',fixture.read_bytes())
s=(D/'preadd_control_tests.cpp').read_text().replace('"transport_fixture.hpp"','"tag_preadd_fixture.hpp"')
s=s.replace(' auto scope=e::SdkSharedQuotaScope::create();check(bool(scope),"Actual SDK origin");auto budget=(*scope)->acquire(7,rate);check(bool(budget),"Actual common budget");', ''' std::unique_ptr<e::SdkSharedQuotaScope> scope;std::shared_ptr<e::SdkSharedTagBudget> budget;
 if(e::SdkSharedQuotaScope::supported()){
  auto created=e::SdkSharedQuotaScope::create();check(bool(created),"Actual SDK origin");scope=std::move(*created);
  auto acquired=scope->acquire(7,rate);check(bool(acquired),"Actual common budget");budget=*acquired;
 }''').replace('rate,*budget','rate,budget').replace('check(bool((*scope)->close()),"Origin close");','if(scope){check(bool(scope->close()),"Origin close");}')
stage('preview/native/tests/sdk_preadd_tests.cpp',s.encode())
stage('preview/native/tests/sdk_preadd_edge_tests.cpp',(D/'preadd_edge_tests.cpp').read_text().replace('"transport_fixture.hpp"','"tag_preadd_fixture.hpp"').encode())
cmake=subprocess.check_output(['git','-C',str(N),'show',parent+':preview/native/tests/CMakeLists.txt'],text=True)
cmake+='''
if(TARGET biglybtc_local_engine)
 foreach(kind controls edges)
  if(kind STREQUAL "controls")
   set(preadd_source sdk_preadd_tests.cpp)
  else()
   set(preadd_source sdk_preadd_edge_tests.cpp)
  endif()
  add_executable(biglybtc-sdk-tag-preadd-${kind}-tests ${preadd_source})
  target_link_libraries(biglybtc-sdk-tag-preadd-${kind}-tests PRIVATE biglybtc_local_engine LibtorrentRasterbar::torrent-rasterbar Threads::Threads)
  target_compile_options(biglybtc-sdk-tag-preadd-${kind}-tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
  add_test(NAME actual-sdk-tag-preadd-${kind} COMMAND biglybtc-sdk-tag-preadd-${kind}-tests)
  set_tests_properties(actual-sdk-tag-preadd-${kind} PROPERTIES TIMEOUT 90)
 endforeach()
endif()
'''
stage('preview/native/tests/CMakeLists.txt',cmake.encode())
for name in paths:
 target=N/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(S/name,target)
(D/'native-source-paths.json').write_text(json.dumps(paths,indent=2)+'\n')
print('Installed',len(paths),'explicit Native files;SDK vendor and profile controller unchanged')
