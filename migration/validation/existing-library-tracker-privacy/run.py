"""Focused wire regression using already qualified production archives; no rebuild of the application.

Run: make --jobserver-style=fifo -j3 -f batch.mk
Logs, commands, source/dependency hashes and Qt XML are retained; test ELF/object
are disposable. Requires the configured normal-Host native cache and Qt 6.
"""
from pathlib import Path
from types import SimpleNamespace
import argparse,json,os,runpy,shlex,subprocess,time,xml.etree.ElementTree as ET
H=Path(__file__).resolve().parent
R=Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC'
C=SimpleNamespace(**runpy.run_path(str(R/'build/i2p-discovery-20261101/p08-tag-history-integration-20261106/common.py')))

def run(lane,attempt):
 C.resources();C.install_signals();cache=R/'build/i2p-provider-attachment-20261029/native-followup-20261030/caches'/lane
 lock=C.lock_cache(cache);out=H/lane/attempt;out.mkdir(parents=True,exist_ok=False)
 source=H/'tracker_privacy_tests.cpp';source_map=C.P['sources']();original=C.P['original']()
 rows=[shlex.split(s) for s in subprocess.check_output(['ninja','-C',str(cache),'-t','commands','biglybtc-ordinary-daily-host-tests'],text=True).splitlines()]
 compiles=[a for a in rows if '-c' in a and any(s.endswith('/ordinary_daily_host_tests.cpp') for s in a)]
 links=[a for a in rows if '-o' in a and a[a.index('-o')+1]=='tests/biglybtc-ordinary-daily-host-tests'];assert len(compiles)==len(links)==1
 cc=compiles[0];link=links[0];assert link[:2]==[':','&&'];link=link[2:];assert link[-2:]==['&&',':'];link=link[:-2]
 moc=Path('/opt/Qt/6.12.0/gcc_64/libexec/moc');obj=out/'privacy.o';binary=out/'privacy-tests';xml=out/'result.xml'
 includes=[s for s in cc if s.startswith('-I')]+['-I'+str(N/'preview/native/tests'),'-I'+str(out)]
 definitions=[s for s in cc if s.startswith('-D')];compile=[];i=0
 while i<len(cc):
  a=cc[i]
  if a in ('-MD',):i+=1;continue
  if a in ('-MT','-MF'):i+=2;continue
  if a=='-o':compile+=['-o',str(obj)];i+=2;continue
  if a=='-c':compile+=['-c',str(source)];i+=2;continue
  if a.startswith('-O') or a=='-g':i+=1;continue
  compile.append(a);i+=1
 compile+=['-O1','-g0','-I'+str(out),'-I'+str(N/'preview/native/tests')]
 linked=[];i=0
 while i<len(link):
  a=link[i]
  if a.startswith('-Wl,--dependency-file=') or a.endswith('mocs_compilation.cpp.o'):i+=1;continue
  if a.endswith('/ordinary_daily_host_tests.cpp.o'):linked.append(str(obj));i+=1;continue
  if a=='-o':linked+=['-o',str(binary)];i+=2;continue
  linked.append(a);i+=1
 dependencies={}
 for a in linked:
  p=Path(a) if a.startswith('/') else cache/a
  if a.endswith(('.a','.so','.so.2.0.12','.so.6.12.0')) and p.is_file():dependencies[str(p)]=C.sha(p)
 env=dict(os.environ,QT_QPA_PLATFORM='offscreen',PYTHONDONTWRITEBYTECODE='1')
 if lane=='san':env.update(ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
 result={'status':'RUNNING','lane':lane,'source':C.pin(source),'runner':C.pin(Path(__file__).resolve()),'original':original,'production_source_sha256':C.sha(R/'build/existing-library-20261107/source-final.json'),'production_commit_at_start':C.P['git']('rev-parse','HEAD'),'dependencies_sha256':dependencies,'commands':[]};started=time.monotonic()
 try:
  for name,argv in [('moc',[str(moc),*definitions,*includes,str(source),'-o',str(out/'tracker_privacy_tests.moc')]),('compile',['nice','-n','5',*compile]),('link',['nice','-n','5',*linked]),('wire-tests',[str(binary),'-o',str(xml)+',xml'])]:
   row=C.run(argv,cache,out/(name+'.log'),1000,lock,env=env);result['commands'].append(row);C.require_success(row)
  root=ET.parse(xml).getroot();cases=[]
  for f in root.iter('TestFunction'):
   for incident in f.findall('Incident'):
    assert incident.get('type')=='pass',(f.get('name'),incident.findtext('Description'))
    if f.get('name') not in ('initTestCase','cleanupTestCase'):cases.append(incident.findtext('DataTag'))
  assert set(cases)=={prefix+family+protocol for prefix in ('private-','public-auth-') for family in ('IPv4-','IPv6-') for protocol in ('HTTP','UDP')} and len(cases)==8
  assert C.P['sources']()==source_map and C.P['original']()==original
  assert all(C.sha(p)==s for p,s in dependencies.items())
  result.update(status='PASS_PRIVATE_TRACKER_CREDENTIAL_PUBLIC_WIRE_ISOLATION',cases=cases,xml=C.pin(xml),image=C.pin(binary))
 except BaseException as e:result.update(status='FAIL',failure=repr(e))
 finally:
  result['elapsed_seconds']=round(time.monotonic()-started,3);C.save(out/'result.json',result)
  print(json.dumps({k:result.get(k) for k in ('status','lane','cases','elapsed_seconds','failure')}),flush=True)
 if result['status']=='FAIL':raise SystemExit(1)
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('lane',choices=('gcc','san'));p.add_argument('--attempt',default='r1');a=p.parse_args();run(a.lane,a.attempt)
