"""Check the recorded exits and exact log bytes of completed refusal controls."""
from pathlib import Path
import hashlib,json
D=Path(__file__).resolve().parent
rows=json.loads((D/'admission-control-receipt.json').read_text());assert len(rows)==14
for row in rows:
 path=D/'admission-controls'/(row['name']+'.log');assert row['exit']!=0 and row['expected'] in path.read_text() and hashlib.sha256(path.read_bytes()).hexdigest()==row['log_sha256']
 assert not (D/'admission-controls'/(row['name']+'.hpp')).exists()
r=json.loads((D/'runtime-relocation-receipt.json').read_text());assert len(r['cases'])==5 and all(x['exit']==0 for x in r['cases']) and not r['original_sdk_changed'] and r['temporary_copies_removed']
assert [x['expected_refusal'] for x in r['cases']]==[False,True,True,True,True]
print('PASS_SHARED_ROLE_RECORDED_CONTROLS 14 configure/CLI refusals and5 actual image cases;exact logs and exits')
