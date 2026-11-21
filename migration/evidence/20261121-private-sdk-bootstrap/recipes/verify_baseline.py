from pathlib import Path
import hashlib,json
D=Path(__file__).resolve().parent;R=D.parents[1];W=R/'build/public-nat-20261114'
rows=json.loads((D/'before-controls.json').read_text());assert [r['mode'] for r in rows]==['direct','service']
for row in rows:
 log=W/('private-bootstrap-before-'+row['mode']+'.log');data=log.read_text();assert row['actual_exit']==row['expected_exit']==1
 assert hashlib.sha256(log.read_bytes()).hexdigest()==row['log_sha256'] and 'storage_untouched=0' in data
 if row['mode']=='service':assert 'source_gate_released=0' in data
print('PASS_RECORDED_BEFORE_BOOTSTRAP two actual constructor negative controls;exact exits and logs')
