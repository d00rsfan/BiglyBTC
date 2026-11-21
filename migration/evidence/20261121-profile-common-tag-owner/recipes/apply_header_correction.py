from pathlib import Path
import hashlib,json,shutil
D=Path(__file__).resolve().parent;N=D.parents[1].parent/'BiglyBTC';S=D/'native-staged';O=D/'native-overlay-qualified';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();inputs=json.loads((D/'verification-inputs.json').read_text());paths=json.loads((D/'native-source-paths.json').read_text());P=json.loads((D/'pre-implementation.json').read_text())
assert all(sha(N/p)==h for p,h in inputs.items())
for name in ['engine/private_tag_rate_source.hpp','services/i2p_provider/profile_source.hpp']:
 path='preview/native/'+name;assert sha(N/path)==P['source_before_sha256'][path];target=S/path;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(O/name,target);shutil.copy2(target,N/path);paths.append(path);inputs[path]=sha(N/path)
(D/'verification-inputs.json').write_text(json.dumps(inputs,indent=2)+'\n');(D/'native-source-paths.json').write_text(json.dumps(paths,indent=2)+'\n');print('Corrected two SDK forward declarations;11 explicit Native paths')
