"""Resume only this owned SAN supervisor after the GCC result is durable."""
from pathlib import Path
import json,os,signal,time
h=Path(__file__).resolve().parent;p=h/'secondary-test-serialization.json'
while not (h/'runs/gcc-r6/result.json').exists():time.sleep(2)
d=json.loads(p.read_text());assert d['resumed_epoch'] is None
pid=d['san_lane_pid'];args=(Path('/proc')/str(pid)/'cmdline').read_bytes().split(b'\0')
assert args[1:]==[str(h/'run-r6.py').encode(),b'san',b'--attempt',b'r6',b''],args
os.kill(pid,signal.SIGCONT);d.update(resumed_epoch=time.time(),resume_reason='GCC result durable after test child join',gcc_status=json.loads((h/'runs/gcc-r6/result.json').read_text())['status'])
p.write_text(json.dumps(d,indent=2)+'\n');print('RESUMED_OWNED_SAN',pid,d['gcc_status'],flush=True)
