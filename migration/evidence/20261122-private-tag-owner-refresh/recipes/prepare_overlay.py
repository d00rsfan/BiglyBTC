from pathlib import Path
import hashlib,json,shutil,subprocess
D=Path(__file__).resolve().parent;N=D.parents[1].parent/'BiglyBTC/preview/native';O=D/'native-overlay';pins={}
for name in subprocess.check_output(['rg','--files',str(N),'-g','*.hpp','-g','*.h','-g','*.inc'],text=True).splitlines():
 source=Path(name);relative=source.relative_to(N);target=O/relative;target.parent.mkdir(parents=True,exist_ok=True);data=source.read_bytes();pins[str(relative)]=hashlib.sha256(data).hexdigest()
 if not target.exists()or target.read_bytes()!=data:shutil.copy2(source,target)
for source in (D/'drafts').rglob('*'):
 if source.is_file():
  target=O/source.relative_to(D/'drafts');target.parent.mkdir(parents=True,exist_ok=True)
  if not target.exists()or target.read_bytes()!=source.read_bytes():shutil.copy2(source,target)
(D/'overlay-baseline-headers.json').write_text(json.dumps(pins,indent=2)+'\n');print('Copied',len(pins),'real Native headers and explicit private refresh drafts')
