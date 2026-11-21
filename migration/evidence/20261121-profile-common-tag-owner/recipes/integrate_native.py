from pathlib import Path
import hashlib,json,subprocess,shutil
D=Path(__file__).resolve().parent;N=Path('/home/yurii/Java/BiglyBTC');P=json.loads((D/'pre-implementation.json').read_text());S=D/'native-staged';O=D/'native-overlay-qualified';paths=[]
assert subprocess.check_output(['git','-C',str(N),'rev-parse','HEAD'],text=True).strip()==P['native_parent']
assert not subprocess.check_output(['git','-C',str(N),'status','--porcelain'],text=True).strip()
for path,digest in P['source_before_sha256'].items():assert hashlib.sha256((N/path).read_bytes()).hexdigest()==digest,path
def stage(name,data):
 p=S/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data);paths.append(name)
for relative,digest in json.loads((D/'final-source-sha256.json').read_text()).items():
 data=(O/relative).read_bytes();assert hashlib.sha256(data).hexdigest()==digest;stage('preview/native/'+relative,data)
stage('preview/native/tests/common_tag_owner_tests.cpp',(D/'common_owner_tests_final.cpp').read_bytes())
stage('preview/native/tests/common_tag_service_tests.cpp',(D/'service_owner_tests_v2.cpp').read_bytes())
s=subprocess.check_output(['git','-C',str(N),'show',P['native_parent']+':preview/native/tests/CMakeLists.txt'],text=True)
s+='''
# Common profile controller injection is also qualified on the selected ABI1
# runtime. Explicit private service admission requires the all-role ABI2 build.
if(TARGET biglybtc_desktop_host AND TARGET biglybtc_local_engine)
 add_executable(biglybtc-common-tag-owner-tests common_tag_owner_tests.cpp)
 target_include_directories(biglybtc-common-tag-owner-tests PRIVATE "${PROJECT_SOURCE_DIR}/services/i2p_provider" "$<TARGET_PROPERTY:biglybtc_desktop_host,INCLUDE_DIRECTORIES>")
 target_compile_definitions(biglybtc-common-tag-owner-tests PRIVATE BIGLYBTC_I2P_TEST_KEYS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/i2p-datagram" "$<TARGET_PROPERTY:biglybtc_desktop_host,COMPILE_DEFINITIONS>")
 target_link_libraries(biglybtc-common-tag-owner-tests PRIVATE biglybtc_desktop_host LibtorrentRasterbar::torrent-rasterbar Qt6::Core Qt6::Network)
 target_compile_options(biglybtc-common-tag-owner-tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
 foreach(mode close no-checkpoint destructor)
  add_test(NAME actual-common-tag-owner-${mode} COMMAND biglybtc-common-tag-owner-tests ${mode})
  set_tests_properties(actual-common-tag-owner-${mode} PROPERTIES TIMEOUT 90)
 endforeach()
 if(BIGLYBTC_SHARED_QUOTA_ALL_ROLES AND TARGET biglybtc_i2p_provider_owner)
  add_executable(biglybtc-common-tag-service-tests common_tag_service_tests.cpp)
  target_include_directories(biglybtc-common-tag-service-tests PRIVATE "${PROJECT_SOURCE_DIR}/services/i2p_provider" "$<TARGET_PROPERTY:biglybtc_desktop_host,INCLUDE_DIRECTORIES>")
  target_compile_definitions(biglybtc-common-tag-service-tests PRIVATE BIGLYBTC_I2P_TEST_KEYS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/i2p-datagram" "$<TARGET_PROPERTY:biglybtc_desktop_host,COMPILE_DEFINITIONS>")
  target_link_libraries(biglybtc-common-tag-service-tests PRIVATE biglybtc_desktop_host biglybtc_i2p_provider_owner LibtorrentRasterbar::torrent-rasterbar Qt6::Core Qt6::Network)
  target_compile_options(biglybtc-common-tag-service-tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
  foreach(mode tagged foreign default)
   add_test(NAME actual-common-tag-service-${mode} COMMAND biglybtc-common-tag-service-tests ${mode})
   set_tests_properties(actual-common-tag-service-${mode} PROPERTIES TIMEOUT 90)
  endforeach()
 endif()
endif()
'''
stage('preview/native/tests/CMakeLists.txt',s.encode())
for relative in ['tests/owned_i2p_profile_fixture.hpp','tests/i2p_classic_host_tests.cpp']:
 name='preview/native/'+relative;s=subprocess.check_output(['git','-C',str(N),'show',P['native_parent']+':'+name],text=True)
 if relative.endswith('.hpp'):s=s.replace('#include "profile_source.hpp"','#include "profile.hpp"\n#include "biglybtc/library.hpp"')
 else:s=s.replace('#include "owned_i2p_profile_fixture.hpp"','#include "owned_i2p_profile_fixture.hpp"\n#include "profile_source.hpp"')
 stage(name,s.encode())

for name in paths:
 p=N/name;p.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(S/name,p)
(D/'native-source-paths.json').write_text(json.dumps(paths,indent=2)+'\n');(D/'verification-inputs.json').write_text(json.dumps({p:hashlib.sha256((N/p).read_bytes()).hexdigest() for p in paths},indent=2)+'\n')
print('Installed',len(paths),'explicit paths;common lifetime and explicit service API only;Host activation remains pending')
