#!/usr/bin/env python3
"""Root-only private window relocation; no builds or subjects on import."""
import argparse,importlib.util,os,pathlib,re,shutil,xml.etree.ElementTree as ET
W=pathlib.Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('window_batch',W/'run-r3.py');b=importlib.util.module_from_spec(spec);spec.loader.exec_module(b);s=b.s;R=b.R;N=b.N
PLAN=R/'build/mainline-host-cohort-20261028/amendments/provider-r2-recipes/private/plan.json'
QT_PROOF=R/'build/mainline-host-cohort-20261028/amendments/provider-r2-recipes/qualification/runs/gcc-r4/runtime-images-before.json'
STATUS='PASS_PRIVATE_PUBLIC_BOUND_STREAMING_WINDOW'
def bundle(root):
 d=s.load(root/'BUILD_MANIFEST.json');assert d['distribution_scope']=='private-qualification-only'
 assert {str(p.relative_to(root)):s.sha(p) for p in root.rglob('*') if p.is_file() and p.name!='BUILD_MANIFEST.json'}==d['files_sha256'];return d

def main():
 p=argparse.ArgumentParser();p.add_argument('--attempt',required=True);p.add_argument('--source-commit',required=True)
 for name in ('adoption','reference','gcc-result','clang-result','san-result'):p.add_argument('--'+name,type=pathlib.Path,required=True)
 a=p.parse_args();assert re.fullmatch('[A-Za-z0-9_-]+',a.attempt);snapshot=s.source_snapshot(a.source_commit);assert s.load(a.adoption)['source_commit']==a.source_commit
 results={};maps={};inventories={}
 for lane in ('gcc','clang','san'):
  path=getattr(a,lane+'_result');assert path.is_absolute();results[lane],maps[lane],inventories[lane]=b.verify_result(path,lane,a.source_commit,a.adoption,a.reference)
 gcc=a.gcc_result.parent;blocks={};active=None
 for line in (gcc/'ctest-full.log').read_text().splitlines():
  found=re.fullmatch(r'\d+/\d+ Testing: (.+)',line)
  if found:active=found.group(1);assert active not in blocks;blocks[active]=[]
  elif active is not None:blocks[active].append(line)
 assert set(blocks)==set(b.SUITES)
 out=W/'private-runs'/a.attempt;out.mkdir(parents=True,exist_ok=False);env=s.environment('gcc')
 for key in ('CMAKE_BUILD_PARALLEL_LEVEL','ASAN_OPTIONS','LSAN_OPTIONS','UBSAN_OPTIONS','LD_DEBUG_OUTPUT','BIGLYBTC_PREVIEW_PROFILE','BIGLYBTC_HTTP_PRIVATE_RUNTIME','BIGLYBTC_I2P_STREAMING_ACCEPTANCE_ROOT','QT_TLS_BACKEND','QML_IMPORT_PATH','QML2_IMPORT_PATH','QT_QPA_PLATFORMTHEME','JAVA_TOOL_OPTIONS','_JAVA_OPTIONS','JDK_JAVA_OPTIONS','CLASSPATH'):env.pop(key,None)
 for key in ('HOME','XDG_CONFIG_HOME','XDG_CACHE_HOME','XDG_RUNTIME_DIR'):
  directory=out/key.lower();directory.mkdir(mode=0o700);env[key]=str(directory)
 runner=s.Runner(out,env);result={'status':'FAIL','source_commit':a.source_commit,'producer_sha256':s.sha(W/'run-r3.py'),'source_adoption_sha256':s.sha(a.adoption),'reference_result_sha256':s.sha(a.reference),'runner_sha256':s.sha(__file__),'builds_executed':False,'provider_rebuilt':False,'historical_passes_pooled':False,'lane_inputs':{lane:{'path':str(getattr(a,lane+'_result')),'sha256':s.sha(getattr(a,lane+'_result'))} for lane in results}}
 try:
  plan=s.load(PLAN);provider=plan['provider'];binding=pathlib.Path(provider['manifest']['path']);assert s.sha(binding)==provider['manifest']['sha256']==b.base.BINDING_SHA
  artifact=pathlib.Path(s.load(binding)['artifact']);assert s.sha(artifact)==provider['artifact_sha256']==b.PROVIDER_SHA;package=plan['debian_package'];assert s.sha(package['path'])==package['sha256']
  old=bundle(N/'dist');oldpin=s.sha(N/'dist/BUILD_MANIFEST.json');candidate=out/'candidate'
  runner.run('stage',['python3','-B',N/'preview/migration/tools/stage_preview.py','--build',results['gcc']['cache'],'--output',candidate,'--libtorrent-package',package['path'],'--source-libtorrent',artifact,'--private-mainline-routing-provider-manifest',binding],timeout=240)
  staged=bundle(candidate);private=candidate/'usr/lib/biglybtc';proof=s.load(QT_PROOF);qt=[v for k,v in proof.items() if re.fullmatch(r'libQt6Test\.so\.6(?:\..+)?',pathlib.Path(k).name)];assert len(qt)==1 and s.sha(qt[0]['path'])==qt[0]['sha256']
  support=out/'test-support';support.mkdir();shutil.copyfile(qt[0]['path'],support/'libQt6Test.so.6')
  env.update(LD_LIBRARY_PATH=os.pathsep.join(map(str,[support,private/'lib',private/'qt/lib',candidate/'dependencies/libtorrent/usr/lib/x86_64-linux-gnu'])),QT_PLUGIN_PATH=str(private/'qt/plugins'),QT_QPA_PLATFORM_PLUGIN_PATH=str(private/'qt/plugins/platforms'))
  wrapper=out/'azureus_public_bound_namespace.py';shutil.copyfile(b.PUBLIC_WRAPPER,wrapper);wrapper_pin={'path':str(wrapper),'sha256':s.sha(wrapper),'source_path':str(b.PUBLIC_WRAPPER)};assert wrapper_pin['sha256']==s.sha(b.PUBLIC_WRAPPER)
  actual=[];copies={}
  with b.subject_phase(out):
   for suite,target in b.SUITES.items():
    image=maps['gcc'][target];copied=out/target
    if target not in copies:shutil.copyfile(image['path'],copied);copied.chmod(0o755);copies[target]=image
    assert s.sha(copied)==image['sha256'];linked=runner.run('loader-'+suite,['ldd',copied],timeout=30).read_text();assert 'not found' not in linked and '/opt/Qt' not in linked
    selected=inventories['gcc'][suite];command=selected['command']
    if suite=='azureus-public-bound':
     assert pathlib.Path(command[1]).resolve()==b.PUBLIC_WRAPPER.resolve() and pathlib.Path(command[2]).resolve()==pathlib.Path(image['path']).resolve();args=[command[0],str(wrapper),str(copied),*command[3:]]
    else:args=[str(copied),*command[1:]]
    properties={x['name']:x['value'] for x in selected.get('properties',[])};budget=float(properties.get('TIMEOUT',180));assert 0<budget<=480
    expected=[x.rsplit('::',1)[-1].removesuffix('()') for x in re.findall(r'^PASS\s*: (.+)$','\n'.join(blocks[suite]),re.M)]
    if expected:args+=['-o',str(out/(suite+'.txt'))+',txt','-o',str(out/(suite+'.xml'))+',junitxml']
    if target==b.ACTOR:env['BIGLYBTC_I2P_STREAMING_ACCEPTANCE_ROOT']=str(out/('actual-java-peer' if suite=='i2p-original-streaming-peer' else 'actual-java-peer-upload'))
    # The Java child inherits loader tracing. Separate PID files prevent its
    # diagnostic stream overflowing; only exact copied native initialization counts.
    prefix='native-loader-'+suite;env.update(LD_DEBUG='libs',LD_DEBUG_OUTPUT=str(out/prefix))
    try:log=runner.run(suite,args,timeout=budget+30)
    finally:env.pop('LD_DEBUG',None);env.pop('LD_DEBUG_OUTPUT',None);env.pop('BIGLYBTC_I2P_STREAMING_ACCEPTANCE_ROOT',None)
    raw=log.read_text(errors='replace');assert not re.search(r'^(?:SKIP\s*:|FAIL!)',raw,re.M)
    traces=[t for t in out.glob(prefix+'.*') if re.search(r'initialize program:\s*'+re.escape(str(copied))+r'\s*$',t.read_text(errors='replace'),re.M)];assert len(traces)==1
    trace=traces[0];loaded={pathlib.Path(v.strip()).resolve() for v in re.findall(r'calling init:\s*([^\n]+)',trace.read_text(errors='replace'))}
    for name in re.findall(r'(libQt6[^ ]+|libtorrent-rasterbar[^ ]+) =>',linked):assert any(p.name==name or p.name.startswith(name+'.') for p in loaded),name
    for path in loaded:
     if path.name.startswith(('libQt6','libqopensslbackend','libqoffscreen','libtorrent-rasterbar')):assert path.is_relative_to(private.resolve()) or path==(support/'libQt6Test.so.6').resolve()
    if re.search(r'libtorrent-rasterbar[^ ]+ =>',linked):assert any(path.name.startswith('libtorrent-rasterbar') and s.sha(path)==b.PROVIDER_SHA for path in loaded)
    record={'suite':suite,'target':target,'exit':0,'arguments':args,'declared_timeout_seconds':budget,'log_sha256':s.sha(log),'native_loader_trace':{'path':str(trace),'sha256':s.sha(trace)},'initialized_images_sha256':{str(path):s.sha(path) for path in loaded if path.name.startswith(('libQt6','libtorrent-rasterbar','libssl.so','libcrypto.so'))}}
    if expected:
     xml=out/(suite+'.xml');names=s.passed_xml(xml);root=ET.parse(xml).getroot();assert len(names)==len(expected) and {x.rsplit('::',1)[-1].removesuffix('()') for x in names}==set(expected)
     assert root.get('tests')==str(len(names)) and all(root.get(k)=='0' for k in ('failures','errors','skipped')) and all(c.get('status','run')=='run' for c in root.iter('testcase'))
     assert not re.search(r'^(?:SKIP\s*:|FAIL!)',(out/(suite+'.txt')).read_text(),re.M);record.update(case_names=names,xml_sha256=s.sha(xml))
    actual.append(record)
  assert len(actual)==len(b.SUITES) and set(copies)==set(b.SUITES.values()) and s.sha(wrapper)==wrapper_pin['sha256']==s.sha(b.PUBLIC_WRAPPER)
  assert bundle(candidate)==staged and bundle(N/'dist')==old and s.sha(N/'dist/BUILD_MANIFEST.json')==oldpin and s.source_snapshot(a.source_commit)==snapshot
  for lane in results:b.verify_result(getattr(a,lane+'_result'),lane,a.source_commit,a.adoption,a.reference)
  result.update(status=STATUS,candidate=str(candidate),candidate_manifest_sha256=s.sha(candidate/'BUILD_MANIFEST.json'),actual_leaf_count=len(staged['files_sha256']),actual_tests=actual,copied_images=copies,namespace_wrapper=wrapper_pin,source_unchanged=True,old_dist_unchanged=oldpin,provider_sha256=s.sha(artifact),subject_phase_sha256=s.sha(out/'subject-phase.json'),java_reference=b.reference_pins(a.reference),peer_evidence=b.peer_evidence(out))
 except BaseException as exc:result['failure']=repr(exc)
 finally:result['commands_sha256']=s.sha(out/'commands.json') if (out/'commands.json').exists() else None;s.write(out/'result.json',result)
 print(result['status'],out/'result.json',flush=True);return 0 if result['status']==STATUS else 1
if __name__=='__main__':raise SystemExit(main())
