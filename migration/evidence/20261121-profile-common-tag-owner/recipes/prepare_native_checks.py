from pathlib import Path
import json,copy
D=Path(__file__).resolve().parent;R=D.parents[1];prior=D.parent/'private-tag-source-20261121';builds=json.loads((prior/'serial-native-builds.json').read_text())
for t in builds:
 t['name']=t['name'].replace('private-tag-','common-tag-');t['argv'][1]='1800';t['argv'].append('biglybtc-common-tag-owner-tests')
 if '-native-'in t['name']:t['argv'].append('biglybtc-common-tag-service-tests')
(D/'serial-native-builds.json').write_text(json.dumps(builds,indent=2)+'\n')
for label in ['native','current']:
 checks=json.loads((prior/(label+'-checks.json')).read_text())
 for t in checks:t['name']=t['name'].replace('private-tag-','common-tag-')
 cache=R/'build/public-shared-quota-runtime-20261121/native-gcc'if label=='native'else R/'build/i2p-provider-attachment-20261029/native-followup-20261030/caches/san';sdk='tag-rate-cross-owner-20261121'if label=='native'else'tag-rate-capacity-20261118'
 env=dict(LD_LIBRARY_PATH=str(R/'build'/sdk/'sdk-install/lib')+':/opt/Qt/6.12.0/gcc_64/lib',QT_QPA_PLATFORM='offscreen')
 for mode in ['close','no-checkpoint','destructor']:
  checks.append(dict(name='common-tag-'+label+'-actual-owner-'+mode,argv=['timeout','90',str(cache/'tests/biglybtc-common-tag-owner-tests'),mode],env=env))
 if label=='native':
  for mode in ['tagged','foreign','default']:checks.append(dict(name='common-tag-'+label+'-actual-service-'+mode,argv=['timeout','90',str(cache/'tests/biglybtc-common-tag-service-tests'),mode],env=env))
 names=[t['name']for t in checks];assert len(set(names))==len(names)
 (D/(label+'-checks.json')).write_text(json.dumps(checks,indent=2)+'\n')
print('Prepared serial full candidate/selected builds and affected regressions with unique names')
