from pathlib import Path
import hashlib,json,subprocess
D=Path(__file__).resolve().parent;N=D.parents[1].parent/'BiglyBTC';P=json.loads((D/'pre-implementation.json').read_text());pins=json.loads((D/'verification-inputs.json').read_text());paths=json.loads((D/'native-source-paths.json').read_text());assert all(hashlib.sha256((N/p).read_bytes()).hexdigest()==h for p,h in pins.items())
for relative in ['tests/owned_i2p_profile_fixture.hpp','tests/i2p_classic_host_tests.cpp']:
 name='preview/native/'+relative;p=N/name;s=p.read_text();P['source_before_sha256'][name]=hashlib.sha256(p.read_bytes()).hexdigest()
 if relative.endswith('.hpp'):
  needle='#include "profile_source.hpp"';assert s.count(needle)==1;s=s.replace(needle,'#include "profile.hpp"\n#include "biglybtc/library.hpp"')
 else:
  needle='#include "owned_i2p_profile_fixture.hpp"';assert s.count(needle)==1;s=s.replace(needle,needle+'\n#include "profile_source.hpp"')
 p.write_text(s);staged=D/'native-staged'/name;staged.parent.mkdir(parents=True,exist_ok=True);staged.write_text(s);paths.append(name);pins[name]=hashlib.sha256(p.read_bytes()).hexdigest()
(D/'pre-implementation.json').write_text(json.dumps(P,indent=2)+'\n');(D/'verification-inputs.json').write_text(json.dumps(pins,indent=2)+'\n');(D/'native-source-paths.json').write_text(json.dumps(paths,indent=2)+'\n')
p=D/'prepare_publish.py';p.write_text(p.read_text().replace('assert len(paths)==11','assert len(paths)==13').replace("f'common-tag-{label}-complete-build.log'","f'common-tag-{label}-qualification-build.log'"))
for label in ['native','current']:
 p=D/(label+'-checks.json');ts=json.loads(p.read_text())
 for t in ts:t['name']+='-fixture'
 p.write_text(json.dumps(ts,indent=2)+'\n')
(D/'fixture-dependency-correction.md').write_text('The selected Public ABI1 target exposed an unnecessary ProfileSource include in owned_i2p_profile_fixture.hpp. Fixture bodies use only ProfileLease and core TorrentHash,so include their actual public headers. i2p_classic_host_tests.cpp now includes ProfileSource explicitly where used. This keeps generated fixtures unchanged and removes all private protocol/header dependencies from the ordinary common-owner target. Initial missing identity.hpp failure retained;application ABI1 already built successfully. Final test binaries are rebuilt and rerun with unique log names.\n')
print('Removed unnecessary private include from shared fixture;added explicit include to actual consumer;13 explicit paths')
