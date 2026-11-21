from pathlib import Path
import hashlib,json,subprocess,shutil
D=Path(__file__).resolve().parent;N=D.parents[1].parent/'BiglyBTC';P=D.parent/'profile-common-tag-owner-20261121';state=json.loads((P/'source-publish.publish-state.json').read_text());assert state.get('pushed'),'D10 must be qualified/pushed first';assert not subprocess.check_output(['git','-C',str(N),'status','--porcelain'],text=True).strip()
parent=subprocess.check_output(['git','-C',str(N),'rev-parse','HEAD'],text=True).strip();baseline=json.loads((D/'prototype-baseline.json').read_text())
for relative,h in baseline['base_source_sha256'].items():assert hashlib.sha256((N/'preview/native'/relative).read_bytes()).hexdigest()==h,relative
baseline.update(native_parent=parent,previous_source=state['source'],parent_milestone_pending=False);(D/'prototype-baseline.json').write_text(json.dumps(baseline,indent=2)+'\n');S=D/'native-staged';paths=[]
def stage(name,data):
 p=S/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data);paths.append(name)
for relative in ['engine/profile_tag_rates.hpp','engine/profile_tag_rates.cpp']:stage('preview/native/'+relative,(D/'native-overlay'/relative).read_bytes())
stage('preview/native/tests/private_tag_live_source_tests.cpp',(D/'live_source_tests.cpp').read_bytes())
s=(N/'preview/native/tests/CMakeLists.txt').read_text();s+='''
if(TARGET biglybtc_local_engine)
 add_executable(biglybtc-private-tag-live-source-tests private_tag_live_source_tests.cpp)
 target_link_libraries(biglybtc-private-tag-live-source-tests PRIVATE biglybtc_local_engine LibtorrentRasterbar::torrent-rasterbar Threads::Threads Qt6::Core)
 target_compile_options(biglybtc-private-tag-live-source-tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
 if(BIGLYBTC_PERSISTENT_SHARED_QUOTA_RUNTIME)
  foreach(mode current rate disabled removed-tag empty cohort sibling-abort foreign remove gate saving close sdk-abort no-lease)
   add_test(NAME actual-private-tag-live-source-${mode} COMMAND biglybtc-private-tag-live-source-tests ${mode})
   set_tests_properties(actual-private-tag-live-source-${mode} PROPERTIES TIMEOUT 90)
  endforeach()
 else()
  add_test(NAME actual-private-tag-live-source-abi1-refusal COMMAND biglybtc-private-tag-live-source-tests)
  set_tests_properties(actual-private-tag-live-source-abi1-refusal PROPERTIES TIMEOUT 90)
 endif()
endif()
''';stage('preview/native/tests/CMakeLists.txt',s.encode())
for path in paths:shutil.copy2(S/path,N/path)
(D/'native-source-paths.json').write_text(json.dumps(paths,indent=2)+'\n');(D/'verification-inputs.json').write_text(json.dumps({path:hashlib.sha256((N/path).read_bytes()).hexdigest()for path in paths},indent=2)+'\n');print('Installed4 explicit bound-source paths;private source rebase/Host activation remain required before chapter/daily completion')
