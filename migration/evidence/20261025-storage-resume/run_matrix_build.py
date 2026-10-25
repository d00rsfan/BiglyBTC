from pathlib import Path
import json,os,subprocess,sys
w=Path(__file__).resolve().parent;p=json.loads((w/'qualified-plan.json').read_text());case,build,jobs=sys.argv[1:];env=os.environ.copy();env.update(BIGLYBTC_STORAGE_BUILD=build,BIGLYBTC_STORAGE_JOBS=jobs);targets=[t for t in p['targets'] if t not in p['primary_only_targets']];raise SystemExit(subprocess.run(['python3',str(w/'compile.py'),case,*targets],env=env).returncode)
