"""Create explicit Native role-admission drafts;does not edit vendor/profile data."""
from pathlib import Path
import hashlib,json,shutil,subprocess
R=Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';D=Path(__file__).resolve().parent;O=D/'native-staged'
parent='6e99907f0773cf15f2e8a89dd86e0dd49aac44c6'
assert subprocess.check_output(['git','-C',str(N),'rev-parse','HEAD'],text=True).strip()==parent
if O.exists():shutil.rmtree(O)
def baseline(name):
 result=subprocess.run(['git','-C',str(N),'show',parent+':'+name],stdout=subprocess.PIPE,stderr=subprocess.DEVNULL)
 return result.stdout if result.returncode==0 else None
pin=json.loads((D/'closed-role-pins.json').read_text());manifest=pin['all-roles-runtime.json'];approval=pin['all-roles-approval.json']
before={};files=[]
def write(name,text):
 full='preview/native/'+name;original=baseline(full);before[full]=hashlib.sha256(original).hexdigest() if original is not None else None
 dst=O/full;dst.parent.mkdir(parents=True,exist_ok=True);dst.write_text(text);files.append(full)
def change(name,old,new):
 p=O/('preview/native/'+name);s=p.read_text() if p.exists() else baseline('preview/native/'+name).decode()
 assert s.count(old)==1,(name,old[:100]);s=s.replace(old,new)
 if p.exists():p.write_text(s)
 else:write(name,s)
write('engine/runtime-binding/verify_runtime_manifest_shared_roles.py',(D/'verify_runtime_manifest_shared_roles.py').read_text())
write('engine/runtime-binding/provider-shared-quota-all-roles-runtime.json',(D/'all-roles-runtime.json').read_text())
write('engine/runtime-binding/provider-shared-quota-all-roles-approval.json',(D/'all-roles-approval.json').read_text())
name='engine/runtime-binding/PersistentRuntimeBinding.cmake'
change(name,'if(BIGLYBTC_PERSISTENT_SHARED_QUOTA_RUNTIME AND',
 'option(BIGLYBTC_SHARED_QUOTA_ALL_ROLES "Exact separately qualified same-image shared SDK roles" OFF)\nif(BIGLYBTC_SHARED_QUOTA_ALL_ROLES AND (NOT BIGLYBTC_PERSISTENT_SHARED_QUOTA_RUNTIME OR NOT BIGLYBTC_PERSISTENT_TAG_RATE_RUNTIME OR NOT BIGLYBTC_PERSISTENT_PUBLIC_CHAT_RUNTIME OR NOT BIGLYBTC_PERSISTENT_MIXED_I2P_RUNTIME OR NOT BIGLYBTC_PRIVATE_I2P_PROVIDER_OWNER OR BIGLYBTC_PERSISTENT_COEXISTENCE_RUNTIME OR BIGLYBTC_PERSISTENT_RUNTIME_MANIFEST STREQUAL ""))\n message(FATAL_ERROR "Shared quota roles require the complete exact Public/Mixed/private cohort")\nendif()\nif(BIGLYBTC_PERSISTENT_SHARED_QUOTA_RUNTIME AND')
change(name,'if(BIGLYBTC_PERSISTENT_PUBLIC_CHAT_RUNTIME AND (BIGLYBTC_PERSISTENT_MIXED_I2P_RUNTIME','if(BIGLYBTC_PERSISTENT_PUBLIC_CHAT_RUNTIME AND NOT BIGLYBTC_SHARED_QUOTA_ALL_ROLES AND (BIGLYBTC_PERSISTENT_MIXED_I2P_RUNTIME')
change(name,'  if(BIGLYBTC_PERSISTENT_PUBLIC_CHAT_RUNTIME)\n    set(_biglybtc_runtime_verifier',
 '  if(BIGLYBTC_SHARED_QUOTA_ALL_ROLES)\n    if(NOT IS_ABSOLUTE "${BIGLYBTC_PERSISTENT_MIXED_I2P_APPROVAL}" OR NOT BIGLYBTC_PRIVATE_I2P_OWNER_APPROVAL STREQUAL BIGLYBTC_PERSISTENT_MIXED_I2P_APPROVAL OR NOT BIGLYBTC_PRIVATE_I2P_OWNER_APPROVAL_SHA256 STREQUAL BIGLYBTC_PERSISTENT_MIXED_I2P_APPROVAL_SHA256)\n      message(FATAL_ERROR "Shared quota roles require one exact approval for every SDK owner")\n    endif()\n    set(_biglybtc_runtime_verifier "${CMAKE_CURRENT_LIST_DIR}/verify_runtime_manifest_shared_roles.py")\n    list(APPEND _biglybtc_role_arguments --approval "${BIGLYBTC_PERSISTENT_MIXED_I2P_APPROVAL}" --approval-sha256 "${BIGLYBTC_PERSISTENT_MIXED_I2P_APPROVAL_SHA256}")\n  elseif(BIGLYBTC_PERSISTENT_PUBLIC_CHAT_RUNTIME)\n    set(_biglybtc_runtime_verifier')
name='engine/persistent_runtime_guard.cpp'
helper='''bool relocatableSharedQuotaRoleArtifact() {
#if defined(BIGLYBTC_SHARED_QUOTA_ALL_ROLES_BOUND) && BIGLYBTC_SHARED_QUOTA_ALL_ROLES_BOUND
 return std::string_view(runtime_binding::manifest)=="'''+manifest+'''"
     &&std::string_view(runtime_binding::mixed_approval_sha256)=="'''+approval+'''"
     &&std::string_view(runtime_binding::sha256)=="05ce92d16e68c4c2f7046ff615dc762bef6d3d012b896bb1915025284ce62f3e"
     &&runtime_binding::bytes==10276784ULL;
#else
 return false;
#endif
}
'''
change(name,'bool relocatableSharedQuotaArtifact() {',helper+'bool relocatableSharedQuotaArtifact() {\n if(relocatableSharedQuotaRoleArtifact()){return true;}')
name='services/CMakeLists.txt'
change(name,'    set(_owned_i2p_source_capsule "f9afc758837d164662ae1ad4fdeec4b842b395ee1f09d30f5dd94d8e382ae5b6")',
 '    set(_owned_i2p_source_capsule "f9afc758837d164662ae1ad4fdeec4b842b395ee1f09d30f5dd94d8e382ae5b6")\n    if(BIGLYBTC_SHARED_QUOTA_ALL_ROLES)\n      set(_owned_i2p_source_capsule "4859f8aadce41c54e91875d1ccda71cb52beb656267e8f3a55f91757375f5ec4")\n    endif()')
name='services/i2p_provider/verify-owner-approval.py'
change(name,'def main():\n','def main():\n    if not __debug__:\n        raise RuntimeError("Private SDK verification requires enabled assertions")\n')
change(name,"    mixed = approval.get('schema') == 'BIGLYBTC_MIXED_I2P_ROLE_APPROVAL_V1'\n    if mixed:",
 "    shared = approval.get('schema') == 'BIGLYBTC_SHARED_QUOTA_ALL_ROLES_APPROVAL_V1'\n    mixed = shared or approval.get('schema') == 'BIGLYBTC_MIXED_I2P_ROLE_APPROVAL_V1'\n    if shared:\n        verifier = Path(__file__).resolve().parents[2] / 'engine/runtime-binding/verify_runtime_manifest_shared_roles.py'\n        sys.dont_write_bytecode = True\n        spec = importlib.util.spec_from_file_location('shared_private_role_reader', verifier)\n        module = importlib.util.module_from_spec(spec)\n        spec.loader.exec_module(module)\n        module.inspect(argparse.Namespace(manifest=Path(approval['runtime_manifest']), approval=approval_path,\n            approval_sha256=approval_sha, library=artifact, include=sdk,\n            header=Path(output).with_name(Path(output).name+'.parent'),receipt=Path(output).with_name(Path(output).name+'.receipt')))\n    elif mixed:")
change(name,"    text += 'namespace biglybtc::services::i2p::provider::private_i2p_runtime_binding {\\n'", "    text += '#define BIGLYBTC_PRIVATE_SHARED_QUOTA_BOUND ' + ('1' if shared else '0') + '\\n'\n    text += 'namespace biglybtc::services::i2p::provider::private_i2p_runtime_binding {\\n'")
name='services/i2p_provider/owner_runtime_guard.cpp'
change(name,'  };\n  constexpr std::size_t required=std::size(symbols);', '''#if defined(BIGLYBTC_PRIVATE_SHARED_QUOTA_BOUND) && BIGLYBTC_PRIVATE_SHARED_QUOTA_BOUND
      ,"biglybtc_i2p_discovery_abi_v1","biglybtc_authorize_i2p_metadata_v1","biglybtc_discovered_i2p_peer_v1",
      "biglybtc_attach_public_udp_v1","biglybtc_detach_public_udp_v1","biglybtc_public_udp_abi_v1","biglybtc_public_udp_bindings_v1","biglybtc_send_public_udp_v1",
      "biglybtc_tag_gate_set_v1","biglybtc_tag_gate_peer_v1","biglybtc_tag_gate_features_v1","biglybtc_tag_gate_notify_peer_v1","biglybtc_tag_class_abi_v1",
      "biglybtc_shared_quota_features_v1","biglybtc_shared_quota_scope_create_v1","biglybtc_shared_quota_scope_close_v1","biglybtc_shared_quota_scope_release_v1","biglybtc_shared_quota_acquire_v1","biglybtc_shared_quota_set_v1","biglybtc_tag_shared_bind_v1"
#endif
  };
  constexpr std::size_t required=std::size(symbols);''')
change(name,'  if (retainedPin) {', '''#if defined(BIGLYBTC_PRIVATE_SHARED_QUOTA_BOUND) && BIGLYBTC_PRIVATE_SHARED_QUOTA_BOUND
  static_assert(required==59);
  using WideMarker=std::uint64_t(*)();
  if(reinterpret_cast<WideMarker>(addresses[51])()!=UINT64_C(0x4242435300020002)
      ||reinterpret_cast<WideMarker>(addresses[52])()!=UINT64_C(0x4242515300010001)){return false;}
#endif
  if (retainedPin) {''')
name='services/shared_udp/CMakeLists.txt'
change(name,'    elseif(BIGLYBTC_PERSISTENT_SHARED_QUOTA_RUNTIME\n       AND _tag_manifest_hash STREQUAL "f5490f29a74c6b63bc4f66e6c9dabb6fb2d4cfc6b267dccd7d1cc01d5c249081"',
 '    elseif(BIGLYBTC_PERSISTENT_SHARED_QUOTA_RUNTIME\n       AND (_tag_manifest_hash STREQUAL "f5490f29a74c6b63bc4f66e6c9dabb6fb2d4cfc6b267dccd7d1cc01d5c249081" OR (BIGLYBTC_SHARED_QUOTA_ALL_ROLES AND _tag_manifest_hash STREQUAL "'+manifest+'"))')
name='tests/RuntimeDeploymentTests.cmake'
change(name,'  if(BIGLYBTC_PERSISTENT_PUBLIC_CHAT_RUNTIME)\n', '  if(BIGLYBTC_SHARED_QUOTA_ALL_ROLES)\n    include("${CMAKE_CURRENT_LIST_DIR}/SharedQuotaRoleRuntimeTests.cmake")\n    return()\n  endif()\n  if(BIGLYBTC_PERSISTENT_PUBLIC_CHAT_RUNTIME)\n')
write('tests/SharedQuotaRoleRuntimeTests.cmake','''if(NOT TARGET biglybtc_i2p_provider_owner)
  message(FATAL_ERROR "Shared quota roles require the actual private owner target")
endif()
add_executable(biglybtc-shared-quota-role-runtime-driver shared_quota_role_runtime_driver.cpp)
target_link_libraries(biglybtc-shared-quota-role-runtime-driver PRIVATE biglybtc_local_engine biglybtc_i2p_provider_owner LibtorrentRasterbar::torrent-rasterbar)
target_include_directories(biglybtc-shared-quota-role-runtime-driver PRIVATE "${PROJECT_SOURCE_DIR}/engine" "${PROJECT_SOURCE_DIR}/services/i2p_provider")
target_compile_options(biglybtc-shared-quota-role-runtime-driver PRIVATE -Wall -Wextra -Wpedantic -Werror)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
if(NOT EXISTS "${BIGLYBTC_CHAT_PARENT_LIBRARY}")
  message(FATAL_ERROR "Shared quota roles require the old-image negative control")
endif()
add_test(NAME shared-quota-role-runtime-deployment COMMAND "${Python3_EXECUTABLE}" -I -B "${CMAKE_CURRENT_SOURCE_DIR}/public_chat_runtime_tests.py" --driver "$<TARGET_FILE:biglybtc-shared-quota-role-runtime-driver>" --provider "$<TARGET_FILE:LibtorrentRasterbar::torrent-rasterbar>" --parent "${BIGLYBTC_CHAT_PARENT_LIBRARY}")
set_tests_properties(shared-quota-role-runtime-deployment PROPERTIES TIMEOUT 100 LABELS "runtime;deployment;roles")
''')
write('tests/shared_quota_role_runtime_driver.cpp','''// SPDX-License-Identifier: GPL-2.0-or-later
#include "persistent_session.hpp"
#include "persistent_runtime_guard.hpp"
#include "owner_runtime_guard.hpp"
#include <iostream>
#include <string_view>
int main(int argc,char** argv){
 using namespace biglybtc::engine;
 const bool expected=!(argc==2&&std::string_view(argv[1])=="reject");
 const bool qualified=detail::persistentRuntimeQualified();
 const bool publicPin=bool(detail::managedExtendedTrafficRuntimePin());
 const bool mixedPin=bool(detail::mixedI2pTransportRuntimePin());
 const bool privatePin=bool(biglybtc::services::i2p::provider::privateOwnerRuntimePin());
 if(qualified!=expected||publicPin!=expected||mixedPin!=expected||privatePin!=expected){return 1;}
 const auto owner=PersistentSession::create({1,1,1});
 if(expected&&!owner){return 2;}
 if(!expected&&(owner||owner.error()!=AccountingError::DependencyUnqualified)){return 3;}
 std::cout<<(expected?"PASS_SHARED_QUOTA_ALL_ROLE_RUNTIME_59":"PASS_SHARED_QUOTA_ALL_ROLE_REFUSAL")<<'\\n';return 0;
}
''')
# Carry the two preexisting selected-cache GCC16 source policies into CMake.
for relative,source in [('services/CMakeLists.txt','i2p_dht/inbound_responder.cpp'),('app/CMakeLists.txt','session_bridge.cpp')]:
 target=O/('preview/native/'+relative)
 text=target.read_text() if target.exists() else baseline('preview/native/'+relative).decode()
 text+='\n# Preserve the selected GCC16 cache policy for inactive optional payload\n# diagnostics. Keep the warning visible;all other diagnostics remain fatal.\nif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 16 AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 17)\n  set_property(SOURCE '+source+' APPEND PROPERTY COMPILE_OPTIONS -Wno-error=maybe-uninitialized)\nendif()\n'
 if target.exists():target.write_text(text)
 else:write(relative,text)
for header in ['services/i2cp_channel/channel.hpp','services/i2cp_leaseset/leaseset.hpp']:
 change(header,'#include "session.hpp"','#include "../i2cp_session/session.hpp"')
for name in ['i2cp_header_order_tests.cpp','i2cp_header_order_reverse.cpp']:
 write('tests/'+name,(D/name).read_text())
write('tests/CMakeLists.txt',baseline('preview/native/tests/CMakeLists.txt').decode()+'\n# Both public I2CP header orders must survive the complete chat/Host includes.\nif(TARGET biglybtc_i2p_provider_owner AND TARGET biglybtc_desktop_host)\n  add_executable(biglybtc-i2cp-header-order-tests i2cp_header_order_tests.cpp i2cp_header_order_reverse.cpp)\n  target_link_libraries(biglybtc-i2cp-header-order-tests PRIVATE biglybtc_desktop_host biglybtc_i2p_provider_owner)\n  target_include_directories(biglybtc-i2cp-header-order-tests BEFORE PRIVATE "${PROJECT_SOURCE_DIR}/services/tor_control")\n  target_include_directories(biglybtc-i2cp-header-order-tests PRIVATE "$<TARGET_PROPERTY:biglybtc_desktop_host,INCLUDE_DIRECTORIES>")\n  target_compile_definitions(biglybtc-i2cp-header-order-tests PRIVATE "$<TARGET_PROPERTY:biglybtc_desktop_host,COMPILE_DEFINITIONS>")\n  target_compile_options(biglybtc-i2cp-header-order-tests PRIVATE -Wall -Wextra -Wpedantic -Werror)\n  add_test(NAME i2cp-chat-header-orders COMMAND biglybtc-i2cp-header-order-tests)\nendif()\n')
app_path=O/'preview/native/app/CMakeLists.txt'
app_path.write_text(app_path.read_text()+'\n# The selected GCC16 cache also keeps this existing allocator diagnostic visible.\nif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 16 AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 17)\n  set_property(SOURCE subscription_acquisition.cpp APPEND PROPERTY COMPILE_OPTIONS -Wno-error=free-nonheap-object)\nendif()\n')
for name,old,new in [('services/i2cp_datagram/messages.hpp','session.hpp','../i2cp_session/session.hpp'),('services/i2p_dht/actor.hpp','channel.hpp','../i2cp_channel/channel.hpp'),('services/i2p_provider/bridge.hpp','channel.hpp','../i2cp_channel/channel.hpp')]:
 change(name,'#include "'+old+'"','#include "'+new+'"')
(D/'native-source-paths.json').write_text(json.dumps(files,indent=2)+'\n')
(D/'native-pre-implementation.json').write_text(json.dumps(dict(native_parent=subprocess.check_output(['git','-C',str(N),'rev-parse','HEAD'],text=True).strip(),original_baseline='cb8c6cb9bb97916cbc7e80338a017200ffc3e71f',before=before,private_tag_fence_changed=False,sdk_vendor_changed=False,new_original_port_claim=False),indent=2)+'\n')
print('Prepared',len(files),'explicit Native role-admission files;active private rates remain fenced')
