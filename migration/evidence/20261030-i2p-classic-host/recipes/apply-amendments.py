from pathlib import Path
import json,hashlib,shutil
T=Path('/home/yurii/Java/BiglyBT/build/host-i2p-20261030');N=Path('/home/yurii/Java/BiglyBTC');d=T/'import-ui-staging/amendments/preserve-supported-start-mode';m=json.loads((d/'manifest.json').read_text())
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
for p,row in m['source_paths'].items():
 assert sha(N/p)==row['before_sha256'],p;assert sha(Path(row['after_path']))==row['after_sha256'],p
for p,row in m['source_paths'].items():shutil.copyfile(row['after_path'],N/p)
m=json.loads((T/'import-staging/amendments/range-loop-warning/manifest.json').read_text())
for p,row in m['source_paths'].items():
 assert sha(N/p)==row['before_sha256'];assert sha(Path(row['after_path']))==row['after_sha256'];shutil.copyfile(row['after_path'],N/p)
print('Preserved supported start selection and fixed range-loop warning; three files adopted.')
