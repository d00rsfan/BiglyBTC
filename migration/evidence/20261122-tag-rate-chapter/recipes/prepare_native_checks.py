from pathlib import Path
import json,subprocess
D=Path(__file__).resolve().parent;B=D/'native-final-gcc'
targets=set(json.loads((D/'focused-qualified-targets.json').read_text()))
missing_images=[name for name in sorted(targets) if not (B/'tests'/name).is_file()]
assert not missing_images,missing_images
ctest=json.loads(subprocess.check_output(['ctest','--test-dir',str(B),'--show-only=json-v1'],text=True))
selected=[]
for row in ctest['tests']:
 cmd=row.get('command',[])
 if not cmd:continue
 if Path(cmd[0]).name in targets or any(Path(x).name in ['original_rate_runtime_inputs_tests.py','shared_quota_runtime_inputs_tests.py','tag_rate_runtime_inputs_tests.py'] for x in cmd):selected.append(row['name'])
assert len(selected)>100 and len(selected)==len(set(selected))
represented={Path(row['command'][0]).name for row in ctest['tests'] if row.get('command') and row['name'] in selected}
assert not targets-represented,sorted(targets-represented)
(D/'selected-native-tests.json').write_text(json.dumps(selected,indent=2)+'\n')
# Exact CTest selection remains inspectable;each registered test keeps its
# original executable arguments,timeout and environment properties.
(D/'selected-native-test-names.txt').write_text('\n'.join(selected)+'\n')
argv=['timeout','7200','ctest','--test-dir',str(B),'--tests-from-file',str(D/'selected-native-test-names.txt'),'--output-on-failure','--parallel','1','--output-junit',str(D/'native-current-tests.xml')]
(D/'native-current-checks-r1.json').write_text(json.dumps([dict(name='tag-chapter-current-native-affected-tests-r1',argv=argv,env={'QT_QPA_PLATFORM':'offscreen','LD_LIBRARY_PATH':str(D.parent/'tag-rate-original-policy-sdk-20261121/sdk-final-install/lib')+':/opt/Qt/6.12.0/gcc_64/lib'})],indent=2)+'\n')
print('Registered affected Native CTest cases:',len(selected))
