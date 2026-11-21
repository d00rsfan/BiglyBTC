from pathlib import Path
import hashlib,json,subprocess,shutil
D=Path(__file__).resolve().parent;N=Path('/home/yurii/Java/BiglyBTC');P=json.loads((D/'pre-implementation.json').read_text());S=D/'native-staged';paths=[]
assert subprocess.check_output(['git','-C',str(N),'rev-parse','HEAD'],text=True).strip()==P['native_parent']
assert not subprocess.check_output(['git','-C',str(N),'status','--porcelain'],text=True).strip()
for path,digest in P['source_before_sha256'].items():assert hashlib.sha256((N/path).read_bytes()).hexdigest()==digest,path
def stage(name,data):
 p=S/name;p.parent.mkdir(parents=True,exist_ok=True)
 if not p.exists()or p.read_bytes()!=data:p.write_bytes(data)
 paths.append(name)
for name in ['profile_tag_rates.hpp','profile_tag_rates.cpp','profile_prepared_tag_attachment.hpp','private_tag_rate_source.hpp','sdk_prepared_tag_attachment.hpp','sdk_tag_rate_group.cpp']:stage('preview/native/engine/'+name,(D/'drafts'/name).read_bytes())
for name in ['profile_source.hpp','profile_source.cpp','profile_owner.hpp','profile_owner.cpp']:stage('preview/native/services/i2p_provider/'+name,(D/'drafts'/name).read_bytes())
stage('preview/native/tests/private_tag_source_tests.cpp',(D/'private_source_tests.cpp').read_bytes())
stage('preview/native/tests/private_tag_owner_storage_tests.cpp',(D/'owner_storage_tests.cpp').read_bytes())
s=subprocess.check_output(['git','-C',str(N),'show',P['native_parent']+':preview/native/tests/CMakeLists.txt'],text=True)
s+='''
if(TARGET biglybtc_local_engine)
 add_executable(biglybtc-private-tag-source-tests private_tag_source_tests.cpp)
 target_link_libraries(biglybtc-private-tag-source-tests PRIVATE biglybtc_local_engine LibtorrentRasterbar::torrent-rasterbar Threads::Threads Qt6::Core)
 target_compile_options(biglybtc-private-tag-source-tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
 foreach(mode match foreign drop bound source saving cohort sibling-abort gate close checking checking-stale)
  add_test(NAME actual-private-tag-source-${mode} COMMAND biglybtc-private-tag-source-tests ${mode})
  set_tests_properties(actual-private-tag-source-${mode} PROPERTIES TIMEOUT 90)
 endforeach()
endif()
if(TARGET biglybtc_i2p_provider_owner AND BIGLYBTC_SHARED_QUOTA_ALL_ROLES)
 add_executable(biglybtc-private-tag-owner-storage-tests private_tag_owner_storage_tests.cpp)
 target_include_directories(biglybtc-private-tag-owner-storage-tests PRIVATE "${PROJECT_SOURCE_DIR}/services/i2p_provider" "$<TARGET_PROPERTY:biglybtc_desktop_host,INCLUDE_DIRECTORIES>")
 target_compile_definitions(biglybtc-private-tag-owner-storage-tests PRIVATE BIGLYBTC_I2P_TEST_KEYS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/i2p-datagram" "$<TARGET_PROPERTY:biglybtc_desktop_host,COMPILE_DEFINITIONS>")
 target_link_libraries(biglybtc-private-tag-owner-storage-tests PRIVATE biglybtc_desktop_host biglybtc_i2p_provider_owner LibtorrentRasterbar::torrent-rasterbar Qt6::Core Qt6::Network)
 target_link_options(biglybtc-private-tag-owner-storage-tests PRIVATE "LINKER:--wrap=_ZN10libtorrent7sessionC1EONS_14session_paramsE")
 target_compile_options(biglybtc-private-tag-owner-storage-tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
 foreach(mode tagged default-fenced foreign-controller held-gate stale-config)
  add_test(NAME actual-private-tag-owner-storage-${mode} COMMAND biglybtc-private-tag-owner-storage-tests ${mode})
  set_tests_properties(actual-private-tag-owner-storage-${mode} PROPERTIES TIMEOUT 90)
 endforeach()
endif()
'''
stage('preview/native/tests/CMakeLists.txt',s.encode())
for name in paths:
 p=N/name;p.parent.mkdir(parents=True,exist_ok=True)
 if not p.exists()or p.read_bytes()!=(S/name).read_bytes():shutil.copy2(S/name,p)
(D/'native-source-paths.json').write_text(json.dumps(paths,indent=2)+'\n');(D/'verification-inputs.json').write_text(json.dumps({p:hashlib.sha256((N/p).read_bytes()).hexdigest() for p in paths},indent=2)+'\n')
print('Installed',len(paths),'explicit Native paths;Host activation and live private source effects remain pending')
