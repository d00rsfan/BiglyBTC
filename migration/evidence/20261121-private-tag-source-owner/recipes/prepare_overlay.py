from pathlib import Path
import hashlib,json,shutil,subprocess
D=Path(__file__).resolve().parent;N=Path('/home/yurii/Java/BiglyBTC/preview/native');O=D/'native-overlay';O.mkdir(exist_ok=True);pins={}
# Real directories preserve relative include semantics;do not symlink a parent
# whose ../ resolution would silently escape back into the original headers.
files=subprocess.check_output(['rg','--files',str(N),'-g','*.hpp','-g','*.h','-g','*.inc'],text=True).splitlines()
for name in files:
 p=Path(name);relative=p.relative_to(N);target=O/relative;target.parent.mkdir(parents=True,exist_ok=True)
 data=p.read_bytes();pins[str(relative)]=hashlib.sha256(data).hexdigest()
 if not target.exists()or target.read_bytes()!=data:shutil.copy2(p,target)
for name in ['profile_tag_rates.hpp','profile_prepared_tag_attachment.hpp','sdk_prepared_tag_attachment.hpp','private_tag_rate_source.hpp']:
 target=O/'engine'/name;source=D/'drafts'/name
 if not target.exists()or target.read_bytes()!=source.read_bytes():shutil.copy2(source,target)
for name in ['profile_source.hpp','profile_source.cpp','profile_owner.hpp','profile_owner.cpp']:
 source=D/'drafts'/name;target=O/'services/i2p_provider'/name
 if not target.exists()or target.read_bytes()!=source.read_bytes():shutil.copy2(source,target)
(D/'overlay-baseline-headers.json').write_text(json.dumps(pins,indent=2)+'\n');print('Prepared',len(pins),'exact baseline headers and explicit overlays;Native untouched')
