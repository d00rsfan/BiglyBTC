from pathlib import Path
import hashlib,json,shutil,copy,subprocess
D=Path(__file__).resolve().parent;N=D.parents[1].parent/'BiglyBTC';O=D/'native-overlay-qualified';shutil.copytree(D/'native-overlay-final',O,dirs_exist_ok=True)
P=json.loads((D/'pre-implementation.json').read_text())
for name in ['engine/private_tag_rate_source.hpp','services/i2p_provider/profile_source.hpp']:
 p=O/name;s=p.read_text();assert s.count('class session_handle;')==1;p.write_text(s.replace('class session_handle;','struct session_handle;'))
 path='preview/native/'+name;P['source_before_sha256'][path]=hashlib.sha256(subprocess.check_output(['git','-C',str(N),'show',P['native_parent']+':'+path])).hexdigest()
(D/'pre-implementation.json').write_text(json.dumps(P,indent=2)+'\n')
pins=json.loads((D/'final-source-sha256.json').read_text());pins.update({name:hashlib.sha256((O/name).read_bytes()).hexdigest()for name in ['engine/private_tag_rate_source.hpp','services/i2p_provider/profile_source.hpp']});(D/'final-source-sha256.json').write_text(json.dumps(pins,indent=2)+'\n')
for variant in ['gcc','san','clang']:
 tasks=json.loads((D/(variant+'-qualified-builds.json')).read_text());ts=[]
 # Correct the two declarations in fixture include graphs. Previously compiled
 # product objects use identical final CPP/ABI;the SDK definition is struct.
 for old in tasks:
  if not any('-fixture-'in old['name']or'-link-'in old['name']for _ in [0]):continue
  t=copy.deepcopy(old);t['name']+='-headers';t['argv']=[x.replace(str(D/'native-overlay-final'),str(O))for x in t['argv']];ts.append(t)
 (D/(variant+'-header-builds.json')).write_text(json.dumps(ts,indent=2)+'\n')
p=D/'integrate_native.py';s=p.read_text().replace("O=D/'native-overlay-final'","O=D/'native-overlay-qualified'");p.write_text(s)
p=D/'prepare_publish.py';s=p.read_text().replace('assert len(paths)==9','assert len(paths)==11').replace("f'common-tag-{label}-build.log'","f'common-tag-{label}-final-build.log'");p.write_text(s)
p=D/'serial-native-builds.json';ts=json.loads(p.read_text())
for t in ts:t['name']=t['name'].replace('-build','-final-build')
p.write_text(json.dumps(ts,indent=2)+'\n')
(D/'header-correction.md').write_text('Clang final fixture exposed two prior class session_handle forward declarations conflicting with the actual SDK struct definition and existing chat_transport_factory declaration. Both are changed to struct. Initial Clang compile failure is retained. Initial isolated Native build was explicitly SIGINT-stopped to keep build inputs frozen;cache is resumed after correction. Final header graphs are recompiled for GCC/scoped SAN/Clang fixtures;already compiled final product CPP and SDK ABI are identical. Both whole Native builds compile current corrected headers.\n')
print('Prepared exact SDK struct forward declarations and retained initial frozen build trees')
