from pathlib import Path
import copy,json,shutil
D=Path(__file__).resolve().parent;O=D/'native-overlay-final';shutil.copytree(D/'native-overlay',O,dirs_exist_ok=True)
(O/'engine/persistent_session.cpp').write_bytes((D/'persistent_session-profile-fix.cpp').read_bytes())
# Freeze separate final source tree while the first scoped SAN build finishes.
for variant in ['gcc','san','clang']:
 base=json.loads((D/('gcc-builds.json' if variant=='gcc' else variant+'-builds.json')).read_text())
 if variant=='gcc':base+=json.loads((D/'gcc-final-builds.json').read_text())
 final=[]
 for old in base:
  t=copy.deepcopy(old);t['name']=t['name']+'-final';t['argv']=[x.replace(str(D/'native-overlay'),str(O)).replace(str(D/'common_owner_tests.cpp'),str(D/'common_owner_tests_final.cpp')).replace(str(D/'service_owner_tests.cpp'),str(D/'service_owner_tests_v2.cpp')).replace(str(D/variant)+'/',str(D/(variant+'-final'))+'/') for x in t['argv']];final.append(t)
 (D/(variant+'-final')).mkdir(exist_ok=True);(D/(variant+'-qualified-builds.json')).write_text(json.dumps(final,indent=2)+'\n')
 checks=json.loads((D/('gcc-final-v2-checks.json' if variant=='gcc' else variant+'-final-v2-checks.json')).read_text())
 for t in checks:
  t['name']=t['name'].replace('-v2','')+'-final';t['argv']=[x.replace(str(D/variant)+'/',str(D/(variant+'-final'))+'/').replace('/service-owner-tests-v2','/service-owner-tests') for x in t['argv']]
 (D/(variant+'-qualified-checks.json')).write_text(json.dumps(checks,indent=2)+'\n')
(D/'final-source-sha256.json').write_text(json.dumps({str(p.relative_to(O)):__import__('hashlib').sha256(p.read_bytes()).hexdigest() for p in [O/'engine/profile_tag_rates.hpp',O/'engine/profile_tag_rates.cpp',O/'engine/persistent_session.hpp',O/'engine/persistent_session.cpp',O/'app/owned_i2p_service.hpp',O/'app/owned_i2p_service.cpp']},indent=2)+'\n')
