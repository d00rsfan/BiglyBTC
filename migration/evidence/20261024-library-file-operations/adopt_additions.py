from pathlib import Path
import hashlib,json,argparse,subprocess
w=Path(__file__).resolve().parent;n=Path('/home/yurii/Java/BiglyBTC');p=argparse.ArgumentParser();p.add_argument('--apply',action='store_true');a=p.parse_args()
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
assert subprocess.check_output(['git','branch','--show-current'],cwd=n,text=True).strip()=='main'
items=[('ui/bridge-tests','staging/preview/native/tests/file_operations_bridge_tests.cpp','preview/native/tests/file_operations_bridge_tests.cpp'),('storage-driver/amendments/selected-normal','file_operations_driver.cpp','preview/native/tests/file_operations_driver.cpp')]
records={}
for capsule,rel,dest in items:
 root=w/capsule;m=json.loads((root/'manifest.json').read_text());src=root/rel;assert sha(src)==m['source_sha256'][dest];assert not (n/dest).exists(),dest
 records[dest]={'source':str(src),'after_sha256':sha(src),'before_sha256':None,'capsule_sha256':sha(root/'manifest.json')}
 if a.apply:(n/dest).write_bytes(src.read_bytes())
cm=n/'preview/native/tests/CMakeLists.txt';before=sha(cm);text=cm.read_text()+'\n'+(w/'ui/bridge-tests/CMake.snippet').read_text()+'''\nif(BIGLYBTC_LOCAL_ENGINE)
  add_executable(biglybtc-file-operations-driver file_operations_driver.cpp)
  target_link_libraries(biglybtc-file-operations-driver PRIVATE biglybtc_profile OpenSSL::Crypto)
  target_compile_options(biglybtc-file-operations-driver PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
''';assert 'add_executable(biglybtc-file-operations-host-tests' not in cm.read_text()
records['preview/native/tests/CMakeLists.txt']={'before_sha256':before,'after_sha256':hashlib.sha256(text.encode()).hexdigest()}
if a.apply:cm.write_text(text)
(w/'additions-receipt.json').write_text(json.dumps(records,indent=2)+'\n');print(('Adopted ' if a.apply else 'Reviewed ')+str(len(records))+' additive source files')
