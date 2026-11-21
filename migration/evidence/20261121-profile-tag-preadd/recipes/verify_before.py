from pathlib import Path
import hashlib,json
D=Path(__file__).resolve().parent;W=D.parents[1]/'build/public-nat-20261114';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
first=json.loads((D/'before-held-gate-receipt.json').read_text());assert first['returncode']==first['expected']==1
assert first['source_sha256']==sha(D/'before-held-gate-profile.cpp')
assert first['log_sha256']==sha(W/'profile-preadd-before-held-gate.log')
rest=json.loads((D/'before-abandoned-receipts.json').read_text());assert len(rest)==2
for row,mode in zip(rest,['abandoned','abandoned-installed']):
 assert row['returncode']==row['expected']==1
 assert row['source_sha256']==sha(D/'before-abandoned-profile.cpp')
 assert row['log_sha256']==sha(W/f'profile-preadd-before-{mode}.log')
replayed=json.loads((D/'replayed-before-receipts.json').read_text());assert len(replayed)==3
for row in replayed:
 mode=row['argv'][-1];body='abandoned' if mode.startswith('abandoned') else 'held-gate'
 assert row['actual_exit']==row['expected_exit']==1
 assert row['body_sha256']==sha(D/f'before-{body}-profile.cpp')
 assert row['log_sha256']==sha(W/f'profile-preadd-replayed-before-{mode}.log')
print('PASS_PROFILE_PREADD_BEFORE_RECEIPTS three actual expected exit1 defects;exact retained bodies rebuilt and all3 reproduced after final fix')
