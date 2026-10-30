#!/usr/bin/env python3
"""Separate optional multi-hash SDK candidate. Never mutates or approves old SDK."""
import argparse, hashlib, importlib.util, json, os, re, shutil, signal, stat, subprocess, sys, time
from pathlib import Path
import xml.etree.ElementTree as ET
sys.dont_write_bytecode=True
R=Path('/home/yurii/Java/BiglyBT');H=Path(__file__).resolve().parent;ROOT=H.parent;D=ROOT/'amendments/tracker-result-admission'
P=R/'build/i2p-provider-attachment-20261029/provider-candidate-r2'
OLD=P
HELPER=R/'build/mainline-dual-identity-provider-20261028/recipe/provider_child.py'
PINS=json.loads((H/'inputs.json').read_bytes())
assert hashlib.sha256(HELPER.read_bytes()).hexdigest()==PINS[str(HELPER)]
spec=importlib.util.spec_from_file_location('retained_provider_tools',HELPER);b=importlib.util.module_from_spec(spec);spec.loader.exec_module(b)
UNITS={'test_biglybtc_private_i2p_owner','test_biglybtc_mainline_identity','test_biglybtc_swarm','test_biglybtc_tracker_response'}
FIXTURES={'mainline':{'mainline-port-bridge'},'socks4a':{'socks4a-provider'}}
FIXTURE_IMAGES={'mainline':'owned-mainline-port-proof','socks4a':'biglybtc-socks4a-provider-tests'}
NEW_EXPORTS=['biglybtc_private_i2p_torrents_abi_v1','biglybtc_admit_private_i2p_torrent_v1','biglybtc_revoke_private_i2p_torrent_v1']
STATUS='ACTUAL_MULTI_I2P_PROVIDER_DIRECT_CONTROLS_PASS_NOT_APPROVED'


def pins():
 for name,pin in PINS.items():b.need(b.sha(name)==pin,'Pinned input changed '+name)
 parent=b.load(P/'source-build-record.json');capsule=b.load(D/'manifest.json')
 b.need(capsule['basis_record']['sha256']==b.sha(P/'source-build-record.json'),'Exact qualified source basis')
 for name,pin in capsule['artifacts_sha256'].items():b.need(b.sha(D/b.key(name))==pin,'Source capsule changed '+name)
 for name,pin in b.load(H/'manifest.json')['artifacts_sha256'].items():b.need(b.sha(H/b.key(name))==pin,'Recipe changed '+name)
 b.need(len(parent['source_inputs_sha256'])==1087 and len(parent['required_exports'])==31,'Existing optional31 SDK')
 b.need(b.tree(P/'source')==parent['source_inputs_sha256'] and b.tree(P/'install',True)==parent['installed_sha256'],'Immutable qualified parent changed')
 b.need(b.sha(parent['compiler']['resolved'])==parent['compiler']['sha256'],'Exact pinned compiler')
 return parent,capsule

def expected(parent,capsule):
 result=dict(parent['source_inputs_sha256'])
 for name,row in capsule['source_paths'].items():
  b.need(result.get(name)==row['before_sha256'] and b.sha(row['before_path'])==row['before_sha256'] and b.sha(row['after_path'])==row['after_sha256'],'Source before/after '+name)
  result[name]=row['after_sha256']
 return result

def candidate(value):
 c=Path(value);b.need(c==ROOT/'candidate-r1','Only the new multi-hash candidate-r1')
 b.need(c.is_absolute() and c.parent.resolve(strict=True)==c.parent,'Canonical candidate parent')
 return c

def verify(c):
 parent,capsule=pins();creation=b.load(c/'creation.json')
 b.need(c.is_dir() and not c.is_symlink() and c.resolve()==c,'Canonical owned candidate')
 for name in ['source','build','install','runs',*[k+s for k in FIXTURES for s in ('-fixture','-build')]]:
  p=c/name
  if os.path.lexists(p):b.need(p.is_dir() and not p.is_symlink() and p.resolve()==p,'Canonical candidate directory '+name)
 b.need(creation['source_inputs_sha256']==expected(parent,capsule)==b.tree(c/'source'),'Closed candidate source')
 b.need(creation['provider_source_manifest_sha256']==b.sha(D/'manifest.json') and creation['recipe_sha256']==b.sha(__file__),'Creation association')
 for name,pin in creation['recipe_inputs_sha256'].items():b.need(b.sha(H/b.key(name))==pin,'Recipe input '+name)
 for name,pin in creation['fixture_inputs_sha256'].items():b.need(b.sha(c/b.key(name))==pin,'Copied exact existing fixture '+name)
 return parent,creation

def prepare(c,a):
 parent,capsule=pins();prior=b.load(ROOT/'manifest.json');previous=b.load(c/'creation.json')
 b.need(previous['recipe_sha256']==b.sha(ROOT/'recipe/provider.py') and previous['provider_source_manifest_sha256']==b.sha(ROOT/'manifest.json'),'Exact completed first attempt source/recipe')
 before=dict(parent['source_inputs_sha256'])
 for name,row in prior['source_paths'].items():before[name]=row['after_sha256']
 b.need(before==previous['source_inputs_sha256']==b.tree(c/'source'),'Actual original source closure')
 run1=c/'runs/r1';failed=b.load(run1/'result.json');commands=b.load(run1/'commands.json')
 b.need(failed['status']=='FAIL' and [(r['name'],r['exit']) for r in commands]==[('configure',0),('configure-reuse',0),('build',0),('install',0),('private-unit-list',0),('provider-tests',8)],'Retain actual socket-EPERM failed r1 without test credit')
 b.need(all(b.sha(c/r['log'])==r['log_sha256'] for r in commands),'Initial command/log bytes')
 b.need(not (c/'creation-r1.json').exists() and not (c/'repair-r2.json').exists(),'One source repair only')
 objects={str(p.relative_to(c)):b.sha(p) for p in (c/'build').rglob('*.o') if p.is_file()}
 allowed={'build/CMakeFiles/torrent-rasterbar.dir/src/torrent.cpp.o','build/test/CMakeFiles/test_biglybtc_private_i2p_owner.dir/test_biglybtc_private_i2p_owner.cpp.o'}
 b.need(allowed<=set(objects),'Exact two affected objects present')
 for name,row in capsule['delta_source_paths'].items():
  b.need(b.sha(c/'source'/name)==row['before_sha256'] and b.sha(row['after_path'])==row['after_sha256'],'Actual two-leaf source correction preimage')
 b.dump(c/'repair-r2.json',dict(initial_creation_sha256=b.sha(c/'creation.json'),failed_r1_result_sha256=b.sha(run1/'result.json'),failed_r1_commands_sha256=b.sha(run1/'commands.json'),
  previous_source_inputs_sha256=before,objects_before_sha256=objects,allowed_changed_objects=sorted(allowed),installed_before_sha256=b.tree(c/'install',True),
  source_correction_manifest_sha256=b.sha(D/'manifest.json'),initial_recipe_manifest_sha256=b.sha(ROOT/'recipe/manifest.json'),no_prior_tests_credited=True))
 shutil.copy2(c/'creation.json',c/'creation-r1.json')
 for name,row in capsule['delta_source_paths'].items():shutil.copy2(row['after_path'],c/'source'/name)
 previous.update(status='SOURCE_RESULT_GUARD_REPAIRED_REQUIRES_FRESH_NINE',source_inputs_sha256=expected(parent,capsule),
  provider_source_manifest_sha256=b.sha(D/'manifest.json'),recipe_sha256=b.sha(__file__),repair_r2_sha256=b.sha(c/'repair-r2.json'),
  recipe_inputs_sha256={n:b.sha(H/n) for n in ('provider.py','jobs.mk','inputs.json','NEXT.txt','test-objects.cmake')})
 b.dump(c/'creation.json',previous);verify(c);print('TWO_SOURCE_LEAVES_REPAIRED_ALL_NINE_REQUIRED',flush=True)


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
 options=['-DCMAKE_FIND_USE_PACKAGE_REGISTRY=FALSE',*extra]
 if Path(source)==c/'source':options.append('-DCMAKE_PROJECT_INCLUDE='+str(H/'test-objects.cmake'))
 return b.config(c,source,build,options)

def protected():
 return dict(head=subprocess.check_output(['git','-C',str(R),'rev-parse','HEAD'],text=True).strip(),index_sha256=b.sha(R/'.git/index'))

def hidden_objects(c):
 result={}
 for name in ('i2p_stream','proxy_base'):
  path=c/('build/CMakeFiles/torrent-rasterbar.dir/src/'+name+'.cpp.o');b.need(path.is_file() and not path.is_symlink(),'Fresh same-provider object '+name);result[str(path.relative_to(c))]=b.sha(path)
 return result

def library_entries(root):
 text=(root/'build/build.ninja').read_text();result={}
 for m in re.finditer(r'^build (CMakeFiles/torrent-rasterbar\.dir/\S+\.o): (CXX_COMPILER\S+) (/\S+) [^\n]*\n((?:  [^\n]*\n)+)',text,re.M):
  values={k:v for k,v in re.findall(r'^  (DEFINES|FLAGS|INCLUDES) = (.*)$',m[4],re.M)}
  result[m[1]]=dict(source=m[3],rule=m[2],**values)
 return result

def reuse_objects(c,out):
 old=library_entries(P);new=library_entries(c);b.need(set(old)==set(new),'Unchanged library translation-unit inventory')
 deptext=subprocess.check_output(['ninja','-C',str(P/'build'),'-t','deps'],text=True)
 blocks={};current=None
 for line in deptext.splitlines():
  m=re.match(r'^(\S+): #deps (\d+), deps mtime (\d+) \((VALID|STALE)\)$',line)
  if m:current=m[1];blocks[current]=dict(count=int(m[2]),mtime=int(m[3]),valid=m[4]=='VALID',deps=[])
  elif line.startswith('    ') and current:blocks[current]['deps'].append(line[4:])
 parent=b.load(P/'source-build-record.json');source=b.load(c/'creation.json')['source_inputs_sha256'];hashes={};reused={};fresh={}
 def pin(path):
  if str(path) not in hashes:hashes[str(path)]=b.sha(path)
  return hashes[str(path)]
 def canonical(value,root):return value.replace(str(root/'source'),'@SOURCE@').replace(str(root/'build'),'@BUILD@')
 for name,entry in old.items():
  b.need({k:canonical(v,P) for k,v in entry.items()}=={k:canonical(v,c) for k,v in new[name].items()},'Identical compiler/defines/flags/includes '+name)
  obj=P/'build'/name;data=blocks.get(name);reasons=[]
  if not data or not data['valid'] or len(data['deps'])!=data['count'] or not obj.is_file() or obj.is_symlink() or obj.stat().st_mtime_ns!=data['mtime']:reasons.append('No valid unchanged object/dependency record')
  else:
   for leaf in data['deps']:
    path=Path(leaf);path=path if path.is_absolute() else P/'build'/path
    if not path.is_file() or path.stat().st_mtime_ns>obj.stat().st_mtime_ns:reasons.append('Dependency newer or absent: '+str(path));continue
    try:rel=str(path.relative_to(P/'source'))
    except ValueError:
     try:path.relative_to(P/'build');reasons.append('Generated parent dependency: '+str(path))
     except ValueError:pin(path)
     continue
    b.need(pin(path)==parent['source_inputs_sha256'][rel],'Parent dependency source changed')
    if source[rel]!=pin(path):reasons.append('Changed source/header: '+rel)
  if reasons:fresh[name]=sorted(set(reasons));continue
  dst=c/'build'/name;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(obj,dst)
  b.need(dst.stat().st_ino!=obj.stat().st_ino,'No shared mutable inode')
  reused[name]=dict(sha256=pin(obj),source=str(Path(entry['source']).relative_to(P/'source')),dependency_count=data['count'],dependency_list_sha256=hashlib.sha256(json.dumps(data['deps'],separators=(',',':')).encode()).hexdigest())
 b.need(reused and fresh,'Both exact reuse and changed translation units')
 proof=dict(parent_record_sha256=b.sha(P/'source-build-record.json'),parent_ninja_deps_sha256=b.sha(P/'build/.ninja_deps'),parent_ninja_file_sha256=b.sha(P/'build/build.ninja'),
  same_normalized_compile_settings=True,dependency_files_sha256=hashes,reused=reused,compiled_fresh=fresh)
 b.dump(out/'object-origins.json',proof)
 # Explicit external object association: never forge Ninja command hashes.
 sources=';'.join(str(c/'source'/row['source']) for row in reused.values());objects=';'.join(str(c/'build'/name) for name in reused)
 (c/'reused-objects.cmake').write_text('set(_reuse_sources "'+sources+'")\nset(_reuse_objects "'+objects+'")\n'+
  'get_target_property(_all_sources torrent-rasterbar SOURCES)\nset(_kept_sources)\nset(_removed 0)\nforeach(_source IN LISTS _all_sources)\n'+
  ' get_filename_component(_absolute "${_source}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")\n if(_absolute IN_LIST _reuse_sources)\n  math(EXPR _removed "${_removed}+1")\n else()\n  list(APPEND _kept_sources "${_source}")\n endif()\nendforeach()\n'+
  'list(LENGTH _reuse_sources _expected)\nif(NOT _removed EQUAL _expected)\n message(FATAL_ERROR "Exact proven reused source set required")\nendif()\n'+
  'set_property(TARGET torrent-rasterbar PROPERTY SOURCES "${_kept_sources}")\nset_source_files_properties(${_reuse_objects} PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)\ntarget_sources(torrent-rasterbar PRIVATE ${_reuse_objects})\n')
 return proof

def build(c,a):
 verify(c);flags=os.environ.get('MAKEFLAGS','');fifo=re.search(r'--jobserver-auth=fifo:([^\s]+)',flags)
 b.need(fifo and re.search(r'(?:^|\s)-j3(?:\s|$)',flags) and stat.S_ISFIFO(Path(fifo[1]).stat().st_mode),'Use GNU Make4.4 FIFO -j3 jobs.mk')
 b.need(not os.environ.get('LD_PRELOAD') and not os.environ.get('LD_AUDIT'),'No loader injection')
 out=c/'runs'/a.attempt;out.mkdir(parents=True,exist_ok=False);(out/'home').mkdir(mode=0o700);rows=[];original=protected()
 result=dict(status='FAIL',native_runtime_approved=False,source_creation_sha256=b.sha(c/'creation.json'),jobserver_makeflags=flags,original_head_index_before=original,previous_private_record_sha256=b.sha(OLD/'source-build-record.json'))
 try:
  versions={n:subprocess.check_output([n,'--version'],text=True).splitlines()[0] for n in ('make','ninja')}
  b.need(re.match(r'GNU Make 4\.4(?:\.|$)',versions['make']) and re.match(r'1\.13(?:\.|$)',versions['ninja']),'Jobserver-compatible tools')
  b.dump(out/'tool-versions.json',versions)
  run(c,out,'configure',config(c,c/'source',c/'build',['-DCMAKE_CXX_STANDARD=23','-DCMAKE_CXX_STANDARD_REQUIRED=ON','-DCMAKE_CXX_EXTENSIONS=OFF',
   '-DCMAKE_INSTALL_PREFIX='+str(c/'install'),'-DCMAKE_INSTALL_LIBDIR=lib','-DBUILD_SHARED_LIBS=ON','-Dbuild_tests=ON',
   '-Dbuild_examples=OFF','-Dbuild_tools=OFF','-Dpython-bindings=OFF','-Dlogging=ON','-Di2p=ON','-Ddht=ON','-Dencryption=ON','-Ddeprecated-functions=ON']),rows)
  b.need(cache(c)['CMAKE_GENERATOR']=='Ninja','Ninja generator')
  parent=b.load(P/'source-build-record.json');b.need({n:cache(c).get(n) for n in parent['abi_cache']}==parent['abi_cache'],'Exact old ABI cache')
  origin=b.load(c/'runs/r1/object-origins.json');repair=b.load(c/'repair-r2.json')
  b.need(set(library_entries(c))==set(origin['compiled_fresh']),'Same affected-library graph')
  run(c,out,'build',['nice','-n','5','cmake','--build',c/'build','--target','torrent-rasterbar',*sorted(UNITS),'test_dht','test_tracker','--','-k','0'],rows,7200)
  run(c,out,'install',['cmake','--install',c/'build'],rows)
  images={str(p.relative_to(c)):b.sha(p) for p in [c/'install'/b.ARTIFACT,*[c/'build/test'/n for n in sorted(UNITS)],c/'build/test/test_dht',c/'build/test/test_tracker']}
  b.dump(out/'images-before.json',images);objects=hidden_objects(c);b.dump(out/'hidden-objects-before.json',objects)
  run(c,out,'private-unit-list',[c/'build/test/test_biglybtc_private_i2p_owner','--list'],rows,60)
  expected_private=set(re.findall(r'^TORRENT_TEST\(([^)]+)\)',(c/'source/test/test_biglybtc_private_i2p_owner.cpp').read_text(),re.M))
  listed_private=[line[3:] for line in (out/'private-unit-list.log').read_text().splitlines() if line.startswith(' - ')]
  b.need(len(expected_private)==10 and len(listed_private)==10 and {n.rsplit('.',1)[-1] for n in listed_private}==expected_private,'All five old and five new private-owner methods')
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
  b.need(all(b.sha(c/'build'/name)==row['sha256'] for name,row in origin['reused'].items()),'Reused object bytes changed');b.need(all(b.sha(name)==pin for name,pin in origin['dependency_files_sha256'].items()),'Reuse dependencies changed')
  b.dump(out/'object-origins.json',origin);b.dump(out/'object-origins-after.json',origin)
  actual_objects={str(p.relative_to(c)):b.sha(p) for p in (c/'build').rglob('*.o') if p.is_file()}
  b.need(set(actual_objects)==set(repair['objects_before_sha256']) and all(actual_objects[n]==v for n,v in repair['objects_before_sha256'].items() if n not in repair['allowed_changed_objects']),'Only exact torrent/private-test objects may change');b.dump(out/'objects-after.json',actual_objects)
  b.need({n:v for n,v in b.tree(c/'install',True).items() if n.startswith('include/')}=={n:v for n,v in repair['installed_before_sha256'].items() if n.startswith('include/')},'All installed headers byte-exact first build')
  verify(c);b.need(hidden_objects(c)==objects and protected()==original,'Objects or original HEAD/index changed');b.dump(out/'hidden-objects-after.json',hidden_objects(c));result.update(status=STATUS,actual_ctest_suites=9,private_owner_methods=10,object_origins_sha256=b.sha(out/'object-origins.json'),reused_objects_from_qualified_sdk=len(origin['reused']),unchanged_library_objects=166,fresh_library_objects=1,two_object_repair_sha256=b.sha(c/'repair-r2.json'),commands_sha256=b.sha(out/'commands.json'),source_unchanged=True,images_unchanged=True,hidden_test_objects_unchanged=True,original_head_index_unchanged=original)
 except BaseException as exc:result['failure']=repr(exc)
 b.dump(out/'result.json',result);b.need(result['status']==STATUS,'Actual attempt retained as failure')

def record(c,a):
 parent,creation=verify(c);out=c/'runs'/a.attempt;result=b.load(out/'result.json');commands=b.load(out/'commands.json')
 b.need(result['status']==STATUS and result['commands_sha256']==b.sha(out/'commands.json'),'Actual successful direct controls required')
 expected_names=['configure','build','install','private-unit-list','provider-tests','test_dht-list','test_tracker-list','wire-controls',* [k+'-'+s for k in FIXTURES for s in ('configure','build','tests')]]
 b.need([x['name'] for x in commands]==expected_names and all(x['exit']==0 and b.sha(c/x['log'])==x['log_sha256'] for x in commands),'Closed actual commands/logs')
 for name,names in [('provider-tests',UNITS),('wire-controls',{'id_enforcement_v4','id_enforcement_v6','parse_i2p_peers'}),*[(k+'-tests',v) for k,v in FIXTURES.items()]]:xml(out/(name+'.xml'),names)
 b.need(b.load(out/'images-before.json')==b.load(out/'images-after.json') and all(b.sha(c/n)==pin for n,pin in b.load(out/'images-before.json').items()),'Actual image pins')
 b.need(b.load(out/'hidden-objects-before.json')==b.load(out/'hidden-objects-after.json')==hidden_objects(c) and result['hidden_test_objects_unchanged'] and result['original_head_index_unchanged']==protected(),'Same-build objects and original HEAD/index required')
 b.need(not (c/'source-build-record.json').exists() and not (c/'approval-proposal').exists(),'Never overwrite record/proposal')
 installed=b.tree(c/'install',True);headers={n:v for n,v in installed.items() if n.startswith('include/libtorrent/')}
 b.need(set(installed)==set(parent['installed_sha256']),'Exact installed SDK closure')
 b.need(all(creation['source_inputs_sha256'].get(n)==pin for n,pin in headers.items()),'SDK/source equality')
 cv=cache(c);b.need({n:cv.get(n) for n in parent['abi_cache']}==parent['abi_cache'],'Same approved ABI/build options')
 artifact=c/'install'/b.ARTIFACT;exports=subprocess.check_output(['nm','-D','--defined-only',str(artifact)],text=True);(c/'exports.txt').write_text(exports)
 required=parent['required_exports']+NEW_EXPORTS;b.need(len(set(required))==34 and set(required)<={line.split()[-1] for line in exports.splitlines() if line.split()},'All31 old and3 new required exports')
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
 origin=b.load(out/'object-origins.json');b.need(origin==b.load(out/'object-origins-after.json') and all(b.sha(c/'build'/n)==v['sha256'] for n,v in origin['reused'].items()) and all(b.sha(n)==pin for n,pin in origin['dependency_files_sha256'].items()),'Exact reused origins remain bound')
 verify(c)
 b.dump(c/'source-build-record.json',dict(schema='BIGLYBTC_PRIVATE_I2P_MULTI_SOURCE_RECORD_V1',status='BUILT_DIRECTLY_TESTED_NOT_APPROVED',
  source_capsule_sha256=b.sha(D/'manifest.json'),parent_record_sha256=b.sha(P/'source-build-record.json'),previous_private_record_sha256=b.sha(OLD/'source-build-record.json'),upstream_commit=parent['upstream_commit'],
  source_root=str(c/'source'),build_root=str(c/'build'),install_root=str(c/'install'),artifact=str(artifact),artifact_sha256=b.sha(artifact),artifact_bytes=artifact.stat().st_size,
  incremental_source_repair_sha256=b.sha(c/'repair-r2.json'),failed_r1_result_sha256=b.sha(c/'runs/r1/result.json'),prebuild_source_review_sha256=b.sha(ROOT/'root-source-review.json'),
  object_origins_sha256=b.sha(out/'object-origins.json'),
  reviewed_inputs_sha256=PINS,recipe_inputs_sha256=creation['recipe_inputs_sha256'],source_review_sha256=None,source_review_required_before_runtime=True,
  source_inputs_sha256=creation['source_inputs_sha256'],installed_sha256=installed,headers_sha256={n.removeprefix('include/'):v for n,v in headers.items()},
  sdk_tree_sha256=hashlib.sha256(json.dumps({n.removeprefix('include/'):v for n,v in headers.items()},sort_keys=True,separators=(',',':')).encode()).hexdigest(),
  required_exports=required,abi_cache=parent['abi_cache'],compiler=parent['compiler'],actual_controls=result,
  evidence_sha256={str(p.relative_to(c)):b.sha(p) for p in [c/'creation.json',c/'creation-r1.json',c/'repair-r2.json',c/'exports.txt',c/'dynamic.txt',c/'ldd.txt',c/'control-loader.json',c/'dependencies.json',*sorted((c/'runs/r1').rglob('*')),*sorted(out.rglob('*'))] if p.is_file()},
  native_runtime_approved=False,shipping_approved=False))
 proposal=c/'approval-proposal';proposal.mkdir();rec=b.load(c/'source-build-record.json')
 b.dump(proposal/'private-i2p-multi-runtime.json',dict(schema=1,purpose='optional-sdk-only-not-approved',source_build_record=str(c/'source-build-record.json'),source_build_record_sha256=b.sha(c/'source-build-record.json'),artifact=rec['artifact'],artifact_sha256=rec['artifact_sha256'],artifact_bytes=rec['artifact_bytes'],source_root=rec['source_root'],install_root=rec['install_root'],native_runtime_approved=False))
 print('MEASURED_CANDIDATE_NOT_APPROVED',b.sha(c/'source-build-record.json'),rec['artifact_sha256'],flush=True)

def main():
 p=argparse.ArgumentParser();p.add_argument('phase',choices=('prepare','build','record'));p.add_argument('--output',required=True);p.add_argument('--attempt',default='r1');a=p.parse_args()
 b.need(re.fullmatch('[a-z0-9][a-z0-9-]{0,40}',a.attempt),'Attempt label');c=candidate(a.output)
 if a.phase=='prepare':prepare(c,a)
 elif a.phase=='build':build(c,a)
 else:record(c,a)
if __name__=='__main__':main()
