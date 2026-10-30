#!/usr/bin/env python3
"""Root-run optional SDK candidate only. No actions on import; never approves it."""
import argparse, hashlib, importlib.util, json, os, re, shutil, signal, stat, subprocess, sys, time
from pathlib import Path
import xml.etree.ElementTree as ET
sys.dont_write_bytecode=True
R=Path('/home/yurii/Java/BiglyBT');H=Path(__file__).resolve().parent
P=R/'build/mainline-dual-identity-provider-20261028/candidate-r2'
ROOT=R/'build/i2p-provider-attachment-20261029'
D=ROOT/'implementation/amendments/deadlines-and-tracker-port/provider'
HELPER=R/'build/mainline-dual-identity-provider-20261028/recipe/provider_child.py'
PINS=json.loads((H/'inputs.json').read_bytes())
assert hashlib.sha256(HELPER.read_bytes()).hexdigest()==PINS[str(HELPER)]
spec=importlib.util.spec_from_file_location('retained_provider_tools',HELPER);b=importlib.util.module_from_spec(spec);spec.loader.exec_module(b)
UNITS={'test_biglybtc_private_i2p_owner','test_biglybtc_mainline_identity','test_biglybtc_swarm','test_biglybtc_tracker_response'}
FIXTURES={'mainline':{'mainline-port-bridge'},'socks4a':{'socks4a-provider'}}
FIXTURE_IMAGES={'mainline':'owned-mainline-port-proof','socks4a':'biglybtc-socks4a-provider-tests'}
NEW_EXPORTS=['biglybtc_private_i2p_owner_abi_v1','biglybtc_configure_private_i2p_owner_v1',
 'biglybtc_read_private_i2p_owner_v1','biglybtc_withdraw_private_i2p_owner_v1']
STATUS='ACTUAL_PRIVATE_I2P_PROVIDER_DIRECT_CONTROLS_PASS_NOT_APPROVED'

def pins():
 for name,pin in PINS.items():b.need(b.sha(name)==pin,'Pinned input changed '+name)
 parent=b.load(P/'source-build-record.json');capsule=b.load(D/'manifest.json');b.verify_capsule(D,capsule)
 b.need(b.tree(P/'source')==parent['source_inputs_sha256'] and b.tree(P/'install',True)==parent['installed_sha256'],'Immutable parent changed')
 for rel,pin in parent['build_files_sha256'].items():b.need(b.sha(P/'build'/b.key(rel))==pin,'Parent build changed '+rel)
 b.need(len(capsule['source_paths'])==13 and capsule['exports']==31 and capsule['private_abi']=='0x42494f01','Private source contract')
 b.need(b.sha('/usr/lib/x86_64-linux-gnu/libssl.so')==b.sha('/usr/lib/x86_64-linux-gnu/libssl.so.3')
  and b.sha('/usr/lib/x86_64-linux-gnu/libcrypto.so')==b.sha('/usr/lib/x86_64-linux-gnu/libcrypto.so.3'),'Same pinned OpenSSL links')
 return parent,capsule

def expected(parent,capsule):
 result=dict(parent['source_inputs_sha256'])
 for name,row in capsule['source_paths'].items():
  b.need(result.get(name)==row['before_sha256'] and b.sha(row['after_path'])==row['after_sha256'],'Source before/after '+name)
  result[name]=row['after_sha256']
 return result

def candidate(value):
 c=Path(value);b.need(c.is_absolute() and c.parent.resolve(strict=True)==c.parent and c.parent==ROOT,'Use a fresh candidate directly under the attachment task')
 b.need(re.fullmatch(r'provider-candidate-[a-z0-9-]+',c.name),'Candidate name')
 return c

def verify(c):
 parent,capsule=pins();creation=b.load(c/'creation.json')
 for name in ['source','build','install','runs',*[k+s for k in FIXTURES for s in ('-fixture','-build')]]:
  p=c/name
  if os.path.lexists(p):b.need(p.is_dir() and not p.is_symlink() and p.resolve()==p,'Canonical owned candidate directory '+name)
 b.need(c.is_dir() and not c.is_symlink() and c.resolve()==c,'Canonical owned candidate')
 b.need(creation['source_inputs_sha256']==expected(parent,capsule)==b.tree(c/'source'),'Closed candidate source changed')
 b.need(creation['provider_source_manifest_sha256']==b.sha(D/'manifest.json') and creation['recipe_sha256']==b.sha(__file__),'Creation association')
 b.need(b.sha(creation['source_review_path'])==creation['source_review_sha256'],'Actual source review changed')
 for name,pin in creation['recipe_inputs_sha256'].items():b.need(b.sha(H/b.key(name))==pin,'Recipe changed '+name)
 for kind in FIXTURES:
  files={n:pin for n,pin in parent['evidence_sha256'].items() if n.startswith(kind+'-fixture/')}
  b.need(b.tree(c/(kind+'-fixture'))=={n.split('/',1)[1]:v for n,v in files.items()},'Owned fixture changed '+kind)
 return parent,creation

def prepare(c,a):
 parent,capsule=pins();b.need(not os.path.lexists(c),'Never overwrite a candidate')
 review=a.source_review.resolve(strict=True);b.need(b.sha(review)==a.source_review_sha256,'Root-selected SOURCE review pin')
 closed=expected(parent,capsule);c.mkdir();(c/'source').mkdir()
 for name in closed:
  dst=c/'source'/b.key(name);dst.parent.mkdir(parents=True,exist_ok=True)
  row=capsule['source_paths'].get(name);shutil.copy2(Path(row['after_path']) if row else P/'source'/name,dst)
 for kind in FIXTURES:
  for name,pin in parent['evidence_sha256'].items():
   if not name.startswith(kind+'-fixture/'):continue
   b.need(b.sha(P/name)==pin,'Parent fixture changed');dst=c/b.key(name);dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(P/name,dst)
 b.dump(c/'creation.json',dict(status='SOURCE_EXPORTED_NOT_BUILT',parent_record_sha256=b.sha(P/'source-build-record.json'),
  provider_source_manifest_sha256=b.sha(D/'manifest.json'),source_inputs_sha256=closed,changed_source_paths=sorted(capsule['source_paths']),
  source_review_path=str(review),source_review_sha256=a.source_review_sha256,recipe_sha256=b.sha(__file__),
  recipe_inputs_sha256={n:b.sha(H/n) for n in ('provider.py','jobs.mk','inputs.json','NEXT.txt')},
  parent_never_written=True,native_runtime_approved=False))
 verify(c);print('SOURCE_EXPORTED_NOT_BUILT',c,flush=True)

def cache(c):
 return {m[1]:m[2] for line in (c/'build/CMakeCache.txt').read_text().splitlines() if (m:=re.match(r'^([^#/][^:]*):[^=]+=(.*)$',line))}

def run(c,out,name,argv,rows,timeout=3600):
 log=out/(name+'.log');command=list(map(str,argv));start=time.monotonic();rc=None;error=None
 env=dict(os.environ);env.pop('CMAKE_BUILD_PARALLEL_LEVEL',None);env.pop('LD_PRELOAD',None);env.pop('LD_AUDIT',None)
 env.update(HOME=str(out/'home'),XDG_CONFIG_HOME=str(out/'home/config'),XDG_CACHE_HOME=str(out/'home/cache'),LD_LIBRARY_PATH=str(c/'install/lib')+':/opt/Qt/6.12.0/gcc_64/lib')
 try:
  with log.open('xb') as stream:
   child=subprocess.Popen(command,cwd=c,env=env,stdout=stream,stderr=subprocess.STDOUT,start_new_session=True)
   try:rc=child.wait(timeout=timeout)
   except BaseException:
    os.killpg(child.pid,signal.SIGTERM)
    try:child.wait(timeout=10)
    except subprocess.TimeoutExpired:os.killpg(child.pid,signal.SIGKILL);child.wait()
    rc=child.returncode;raise
 except BaseException as exc:error=repr(exc)
 row=dict(name=name,args=command,exit=rc,seconds=round(time.monotonic()-start,3),log=str(log.relative_to(c)))
 if log.exists():row['log_sha256']=b.sha(log)
 if error:row['error']=error
 rows.append(row);b.dump(out/'commands.json',rows);print(name,rc,flush=True)
 b.need(error is None and rc==0,'Actual command failed '+name)

def xml(path,names):
 root=ET.parse(path).getroot();cases=list(root.iter('testcase'))
 b.need(len(cases)==len(names) and {x.get('name') for x in cases}==set(names),'Exact XML inventory '+str(path))
 b.need(all(x.get('status')=='run' and not any(x.find(t) is not None for t in ('failure','error','skipped')) for x in cases),'No failed/skipped XML rows')

def config(c,source,build,extra):
 return b.config(c,source,build,['-DCMAKE_FIND_USE_PACKAGE_REGISTRY=FALSE',*extra])

def build(c,a):
 verify(c);flags=os.environ.get('MAKEFLAGS','');fifo=re.search(r'--jobserver-auth=fifo:([^\s]+)',flags)
 b.need(fifo and re.search(r'(?:^|\s)-j3(?:\s|$)',flags) and stat.S_ISFIFO(Path(fifo[1]).stat().st_mode),'Use GNU Make4.4 FIFO -j3 jobs.mk')
 b.need(not os.environ.get('LD_PRELOAD') and not os.environ.get('LD_AUDIT'),'No loader injection')
 out=c/'runs'/a.attempt;out.mkdir(parents=True,exist_ok=False);(out/'home').mkdir(mode=0o700);rows=[]
 result=dict(status='FAIL',native_runtime_approved=False,source_creation_sha256=b.sha(c/'creation.json'),jobserver_makeflags=flags)
 try:
  versions={n:subprocess.check_output([n,'--version'],text=True).splitlines()[0] for n in ('make','ninja')}
  b.need(re.match(r'GNU Make 4\.4(?:\.|$)',versions['make']) and re.match(r'1\.13(?:\.|$)',versions['ninja']),'Jobserver-compatible tools')
  b.dump(out/'tool-versions.json',versions)
  run(c,out,'configure',config(c,c/'source',c/'build',['-DCMAKE_CXX_STANDARD=23','-DCMAKE_CXX_STANDARD_REQUIRED=ON','-DCMAKE_CXX_EXTENSIONS=OFF',
   '-DCMAKE_INSTALL_PREFIX='+str(c/'install'),'-DCMAKE_INSTALL_LIBDIR=lib','-DBUILD_SHARED_LIBS=ON','-Dbuild_tests=ON',
   '-Dbuild_examples=OFF','-Dbuild_tools=OFF','-Dpython-bindings=OFF','-Dlogging=ON','-Di2p=ON','-Ddht=ON','-Dencryption=ON','-Ddeprecated-functions=ON']),rows)
  b.need(cache(c)['CMAKE_GENERATOR']=='Ninja','Ninja generator')
  run(c,out,'build',['nice','-n','5','cmake','--build',c/'build','--target','torrent-rasterbar',*sorted(UNITS),'test_dht','test_tracker','--','-k','0'],rows,7200)
  run(c,out,'install',['cmake','--install',c/'build'],rows)
  images={str(p.relative_to(c)):b.sha(p) for p in [c/'install'/b.ARTIFACT,*[c/'build/test'/n for n in sorted(UNITS)],c/'build/test/test_dht',c/'build/test/test_tracker']}
  b.dump(out/'images-before.json',images)
  run(c,out,'provider-tests',['ctest','--test-dir',c/'build','-R','^('+'|'.join(sorted(UNITS))+')$','--output-on-failure','--output-junit',out/'provider-tests.xml'],rows)
  xml(out/'provider-tests.xml',UNITS)
  # Existing named controls only: never run all test_dht/test_tracker subjects.
  controls=out/'wire-controls';controls.mkdir();entries=[];control_names=[]
  for target,suffixes in [('test_dht',['id_enforcement_v4','id_enforcement_v6']),('test_tracker',['parse_i2p_peers'])]:
   run(c,out,target+'-list',[c/'build/test'/target,'--list'],rows,60)
   listed=[line[3:] for line in (out/(target+'-list.log')).read_text().splitlines() if line.startswith(' - ')]
   for suffix in suffixes:
    matches=[n for n in listed if n.endswith('.'+suffix)];b.need(len(matches)==1,'Exact existing control '+suffix)
    control_names.append(suffix);entries.append('add_test('+suffix+' '+json.dumps(str(c/'build/test'/target))+' "-n" '+json.dumps(matches[0])+')\n')
  (controls/'CTestTestfile.cmake').write_text(''.join(entries))
  run(c,out,'wire-controls',['ctest','--test-dir',controls,'--output-on-failure','--output-junit',out/'wire-controls.xml'],rows);xml(out/'wire-controls.xml',control_names)
  for kind,names in FIXTURES.items():
   extra=['-DLibtorrentRasterbar_DIR='+str(c/'install/lib/cmake/LibtorrentRasterbar')]
   if kind=='mainline':extra+=['-DMAINLINE_PROVIDER_INSTALL='+str(c/'install'),'-DMAINLINE_PROVIDER_SOURCE='+str(c/'source')]
   run(c,out,kind+'-configure',config(c,c/(kind+'-fixture'),c/(kind+'-build'),extra),rows)
   run(c,out,kind+'-build',['nice','-n','5','cmake','--build',c/(kind+'-build'),'--','-k','0'],rows)
   executable=c/(kind+'-build')/FIXTURE_IMAGES[kind];images[str(executable.relative_to(c))]=b.sha(executable);b.dump(out/'images-before.json',images)
   run(c,out,kind+'-tests',['ctest','--test-dir',c/(kind+'-build'),'--output-on-failure','--output-junit',out/(kind+'-tests.xml')],rows);xml(out/(kind+'-tests.xml'),names)
  b.need(all(b.sha(c/n)==pin for n,pin in images.items()),'Tested images changed');b.dump(out/'images-after.json',images)
  verify(c);result.update(status=STATUS,actual_ctest_suites=9,commands_sha256=b.sha(out/'commands.json'),source_unchanged=True,images_unchanged=True)
 except BaseException as exc:result['failure']=repr(exc)
 b.dump(out/'result.json',result);b.need(result['status']==STATUS,'Actual attempt retained as failure')

def record(c,a):
 parent,creation=verify(c);out=c/'runs'/a.attempt;result=b.load(out/'result.json');commands=b.load(out/'commands.json')
 b.need(result['status']==STATUS and result['commands_sha256']==b.sha(out/'commands.json'),'Actual successful direct controls required')
 expected_names=['configure','build','install','provider-tests','test_dht-list','test_tracker-list','wire-controls',* [k+'-'+s for k in FIXTURES for s in ('configure','build','tests')]]
 b.need([x['name'] for x in commands]==expected_names and all(x['exit']==0 and b.sha(c/x['log'])==x['log_sha256'] for x in commands),'Closed actual commands/logs')
 for name,names in [('provider-tests',UNITS),('wire-controls',{'id_enforcement_v4','id_enforcement_v6','parse_i2p_peers'}),*[(k+'-tests',v) for k,v in FIXTURES.items()]]:xml(out/(name+'.xml'),names)
 b.need(b.load(out/'images-before.json')==b.load(out/'images-after.json') and all(b.sha(c/n)==pin for n,pin in b.load(out/'images-before.json').items()),'Actual image pins')
 b.need(not (c/'source-build-record.json').exists() and not (c/'approval-proposal').exists(),'Never overwrite record/proposal')
 installed=b.tree(c/'install',True);headers={n:v for n,v in installed.items() if n.startswith('include/libtorrent/')}
 b.need(set(installed)==set(parent['installed_sha256'])|{'include/libtorrent/biglybtc_private_i2p_owner.hpp','include/libtorrent/aux_/biglybtc_private_i2p_owner.hpp'},'Exact installed SDK closure')
 b.need(all(creation['source_inputs_sha256'].get(n)==pin for n,pin in headers.items()),'SDK/source equality')
 cv=cache(c);b.need({n:cv.get(n) for n in parent['abi_cache']}==parent['abi_cache'],'Same approved ABI/build options')
 artifact=c/'install'/b.ARTIFACT;exports=subprocess.check_output(['nm','-D','--defined-only',str(artifact)],text=True);(c/'exports.txt').write_text(exports)
 required=parent['required_exports']+NEW_EXPORTS;b.need(len(set(required))==31 and set(required)<={line.split()[-1] for line in exports.splitlines() if line.split()},'Exact31 exports')
 dynamic=subprocess.check_output(['readelf','-dW',str(artifact)],text=True);(c/'dynamic.txt').write_text(dynamic)
 b.need(re.search(r'\(SONAME\).*\[libtorrent-rasterbar\.so\.2\.0\]',dynamic),'Provider SONAME')
 b.need(not os.environ.get('LD_PRELOAD') and not os.environ.get('LD_AUDIT'),'No loader injection')
 environment=dict(os.environ,LD_LIBRARY_PATH=str(c/'install/lib')+':/opt/Qt/6.12.0/gcc_64/lib')
 dependencies=subprocess.check_output(['ldd',str(artifact)],text=True,env=environment);(c/'ldd.txt').write_text(dependencies);b.need('not found' not in dependencies,'Resolved provider dependencies')
 loader={};dependency_files={}
 for executable in [*[c/'build/test'/n for n in sorted(UNITS)],*[c/(k+'-build')/v for k,v in FIXTURE_IMAGES.items()]]:
  text=subprocess.check_output(['ldd',str(executable)],text=True,env=environment);b.need('not found' not in text,'Resolved control dependencies')
  matches=re.findall(r'libtorrent-rasterbar\.so\.2\.0 => (/\S+) ',text);b.need(len(matches)==1 and Path(matches[0]).resolve()==artifact,'Actual control binds new candidate')
  loader[str(executable.relative_to(c))]=text
  for file in re.findall(r'(?:=>\s+|^\s*)(/\S+)\s+\(',text,re.M):dependency_files[file]=b.sha(file)
 b.dump(c/'control-loader.json',loader);b.dump(c/'dependencies.json',dependency_files)
 verify(c)
 b.dump(c/'source-build-record.json',dict(schema='BIGLYBTC_PRIVATE_I2P_OWNER_SOURCE_RECORD_V1',status='BUILT_DIRECTLY_TESTED_NOT_APPROVED',
  source_capsule_sha256=b.sha(D/'manifest.json'),parent_record_sha256=b.sha(P/'source-build-record.json'),upstream_commit=parent['upstream_commit'],
  source_root=str(c/'source'),build_root=str(c/'build'),install_root=str(c/'install'),artifact=str(artifact),artifact_sha256=b.sha(artifact),artifact_bytes=artifact.stat().st_size,
  reviewed_inputs_sha256=PINS,recipe_inputs_sha256=creation['recipe_inputs_sha256'],source_review_sha256=creation['source_review_sha256'],
  source_inputs_sha256=creation['source_inputs_sha256'],installed_sha256=installed,headers_sha256={n.removeprefix('include/'):v for n,v in headers.items()},
  sdk_tree_sha256=hashlib.sha256(json.dumps({n.removeprefix('include/'):v for n,v in headers.items()},sort_keys=True,separators=(',',':')).encode()).hexdigest(),
  required_exports=required,abi_cache=parent['abi_cache'],compiler=parent['compiler'],actual_controls=result,
  evidence_sha256={str(p.relative_to(c)):b.sha(p) for p in [c/'creation.json',c/'exports.txt',c/'dynamic.txt',c/'ldd.txt',c/'control-loader.json',c/'dependencies.json',*sorted(out.rglob('*'))] if p.is_file()},
  native_runtime_approved=False,shipping_approved=False))
 proposal=c/'approval-proposal';proposal.mkdir();rec=b.load(c/'source-build-record.json')
 b.dump(proposal/'private-i2p-owner-runtime.json',dict(schema=1,purpose='optional-sdk-only-not-approved',source_build_record=str(c/'source-build-record.json'),source_build_record_sha256=b.sha(c/'source-build-record.json'),artifact=rec['artifact'],artifact_sha256=rec['artifact_sha256'],artifact_bytes=rec['artifact_bytes'],source_root=rec['source_root'],install_root=rec['install_root'],native_runtime_approved=False))
 print('MEASURED_CANDIDATE_NOT_APPROVED',b.sha(c/'source-build-record.json'),rec['artifact_sha256'],flush=True)

def main():
 p=argparse.ArgumentParser();p.add_argument('phase',choices=('prepare','build','record'));p.add_argument('--output',required=True);p.add_argument('--attempt',default='r1');p.add_argument('--source-review',type=Path);p.add_argument('--source-review-sha256');a=p.parse_args()
 b.need(re.fullmatch('[a-z0-9][a-z0-9-]{0,40}',a.attempt),'Attempt label');c=candidate(a.output)
 if a.phase=='prepare':b.need(a.source_review and re.fullmatch('[a-f0-9]{64}',a.source_review_sha256 or ''),'Root-reviewed source receipt required');prepare(c,a)
 elif a.phase=='build':build(c,a)
 else:record(c,a)
if __name__=='__main__':main()
