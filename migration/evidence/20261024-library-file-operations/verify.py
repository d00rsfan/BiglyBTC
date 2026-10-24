#!/usr/bin/env python3
"""Batched compiler and affected regression lane; retain full output on disk."""
import argparse,ast,hashlib,json,os,re,subprocess,time
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('build');p.add_argument('--case',required=True);p.add_argument('--jobs',type=int,default=3);p.add_argument('--focus',nargs='*');p.add_argument('--tests-only',action='store_true');p.add_argument('--capture',action='store_true');p.add_argument('--portable-products',action='store_true');a=p.parse_args()
n=Path('/home/yurii/Java/BiglyBTC');w=Path(__file__).resolve().parent
old=ast.parse((w.parent/'options-20261024/verify.py').read_text())
cases=next(ast.literal_eval(node.value) for node in old.body if isinstance(node,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='cases' for t in node.targets))
cases.update({x:x for x in ['queue-policy','managed-queue','scheduler-service','scheduler-dialog','scheduler-host','scheduler-host-engine','file-operations','torrent-creator','file-operation-dialog','torrent-creation-dialog','file-operations-host']})
selected=a.focus or ['file-operations','torrent-creator','file-operation-dialog','torrent-creation-dialog','file-operations-host',
 'scheduler-service','scheduler-host-engine','managed-queue','queue-policy','managed-download','managed-seeding',
 'managed-library-command-routing','managed-storage','torrent-intake','file-policy-commands','file-priority-ui',
 'desktop-model-and-lifecycle','startup-statistics-worker','overall-accounting-window-worker','persistent-native-peer-session']
assert 1<=a.jobs<=3 and all(c in cases for c in selected)
b=(n/a.build).resolve();env=os.environ.copy();env.update(QT_QPA_PLATFORM='offscreen',ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
if a.capture:
 capture=w/(a.case+'-captures');capture.mkdir(exist_ok=True);env['BIGLYBTC_FILEOPS_CAPTURE_DIRECTORY']=str(capture);env['BIGLYBTC_SCHEDULER_CAPTURE_DIRECTORY']=str(capture)
sources={str(p.relative_to(n)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted((n/'preview/native').rglob('*')) if p.is_file()}
(w/(a.case+'-source.json')).write_text(json.dumps(sources,indent=2)+'\n')
record={'case':a.case,'build':str(b),'selected':selected,'source_manifest_sha256':hashlib.sha256((w/(a.case+'-source.json')).read_bytes()).hexdigest(),'commands':[]}
def run(kind,cmd):
 log=w/(a.case+'-'+kind+'.log');start=time.monotonic()
 with log.open('w') as f: rc=subprocess.run(cmd,cwd=n,env=env,stdout=f,stderr=subprocess.STDOUT).returncode
 record['commands'].append({'command':cmd,'exit':rc,'seconds':round(time.monotonic()-start,2),'log':log.name});(w/(a.case+'-receipt.json')).write_text(json.dumps(record,indent=2)+'\n')
 print(kind+' exit '+str(rc),flush=True)
 if rc:
  lines=log.read_text(errors='replace').splitlines();errors=[l for l in lines if 'error:' in l or 'FAILED:' in l or 'FAIL!' in l or 'Failed' in l];print('\n'.join(errors[:24] or lines[-16:]),flush=True);raise SystemExit(rc)
if not a.tests_only:
 targets=['biglybtc-'+cases[c]+'-tests' for c in selected]+['BiglyBTC','BiglyBTC-headless','biglybtc-torrent-creator-driver']
 if a.portable_products: targets+=['biglybtc-preview-smoke','biglybtc-file-operations-driver']
 run('build',['nice','-n','5','cmake','--build',str(b),'--parallel',str(a.jobs),'--target',*targets,'--','-k','0'])
registered=json.loads(subprocess.check_output(['ctest','--test-dir',str(b),'--show-only=json-v1'],text=True));missing=set(selected)-{t['name'] for t in registered['tests']};assert not missing,missing
run('tests',['nice','-n','5','ctest','--test-dir',str(b),'-R','^('+'|'.join(re.escape(c) for c in selected)+')$','--no-tests=error','--output-on-failure','--verbose','--output-junit',str(w/(a.case+'.xml'))])
print(str(len(selected))+' selected suites completed; '+a.case+'-receipt.json',flush=True)
