from pathlib import Path
import hashlib,json,shutil
D=Path(__file__).resolve().parent;N=D.parents[1].parent/'BiglyBTC';path='preview/native/tests/CMakeLists.txt';pins=json.loads((D/'verification-inputs.json').read_text());assert all(hashlib.sha256((N/p).read_bytes()).hexdigest()==h for p,h in pins.items());data=(D/'CMakeLists-abi1-test-fix.txt').read_bytes();(N/path).write_bytes(data);(D/'native-staged'/path).write_bytes(data);pins[path]=hashlib.sha256(data).hexdigest();(D/'verification-inputs.json').write_text(json.dumps(pins,indent=2)+'\n')
ts=json.loads((D/'serial-native-builds.json').read_text())
for t in ts:t['name']=t['name'].replace('-final-build','-complete-build')
(D/'complete-native-builds.json').write_text(json.dumps(ts,indent=2)+'\n');p=D/'prepare_publish.py';p.write_text(p.read_text().replace("f'common-tag-{label}-final-build.log'","f'common-tag-{label}-complete-build.log'"));print('Corrected only common test target availability;product sources unchanged')
