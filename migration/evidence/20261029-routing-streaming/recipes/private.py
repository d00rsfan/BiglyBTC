#!/usr/bin/env python3
"""Root only: copy qualified routing/Streaming images and test one private stage."""
import argparse,importlib.util,os,pathlib,re,shlex,shutil,xml.etree.ElementTree as ET
R=pathlib.Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';H=pathlib.Path(__file__).resolve().parent.parent
SUPPORT=R/'build/mainline-scrape-20261028/recipe_support_followup.py'
spec=importlib.util.spec_from_file_location('support',SUPPORT);s=importlib.util.module_from_spec(spec);spec.loader.exec_module(s)
batch=None
PLAN=R/'build/mainline-host-cohort-20261028/amendments/provider-r2-recipes/private/plan.json'
QT_PROOF=R/'build/mainline-host-cohort-20261028/amendments/provider-r2-recipes/qualification/runs/gcc-r4/runtime-images-before.json'
def bind_producer(path):
 global batch
 spec=importlib.util.spec_from_file_location('batch',path);batch=importlib.util.module_from_spec(spec);spec.loader.exec_module(batch);return batch
def bundle(root,s=s):
 d=s.load(root/'BUILD_MANIFEST.json');assert d['distribution_scope']=='private-qualification-only'
 assert {str(p.relative_to(root)):s.sha(p) for p in root.rglob('*') if p.is_file() and p.name!='BUILD_MANIFEST.json'}==d['files_sha256'];return d

def main():
 p=argparse.ArgumentParser();p.add_argument('--attempt',required=True);p.add_argument('--source-commit',required=True)
 p.add_argument('--adoption',type=pathlib.Path,required=True);p.add_argument('--producer',type=pathlib.Path,required=True)
 for lane in ('gcc','clang','san'):p.add_argument('--'+lane+'-result',type=pathlib.Path,required=True)
 a=p.parse_args();assert re.fullmatch('[A-Za-z0-9_-]+',a.attempt) and re.fullmatch('[0-9a-f]{40}',a.source_commit)
 batch=bind_producer(a.producer)
 adoption=s.load(a.adoption);source=a.source_commit;assert adoption['source_commit']==source;snapshot=s.source_snapshot(source);maps={};inputs={};blocks_by_lane={};references=[]
 for name,row in adoption['source_paths'].items():assert s.sha(N/name)==row['after_sha256'],name
 for lane in ('gcc','clang','san'):
  result_path=getattr(a,lane+'_result');root=result_path.parent;d=s.load(result_path);suites=batch.suites_for_lane(lane)
  assert len(suites)==(14 if lane=='gcc' else 11)
  assert d['status']=='PASS_ROUTING_STREAMING_COHORT' and d['source_commit']==source and d['lane']==lane and d['actual_suites']==len(suites)
  assert d['source_unchanged'] and d['images_unchanged'] and len(d['suite_names'])==len(suites) and set(d['suite_names'])==set(suites)
  assert s.load(root/'source-before.json')==s.load(root/'source-after.json')==snapshot and d['runner_sha256']==s.sha(a.producer) and d['source_receipt_sha256']==s.sha(a.adoption)
  assert s.sha(root/'tests.xml')==d['tests_xml_sha256'] and s.sha(root/'ctest-full.log')==d['full_ctest_log_sha256'];s.passed_xml(root/'tests.xml',batch.suites_for_lane(lane))
  assert all(c.get('status')=='run' for c in ET.parse(root/'tests.xml').getroot().iter('testcase'))
  assert s.sha(root/'commands.json')==d['commands_sha256'];commands=s.load(root/'commands.json');assert [x['name'] for x in commands]==['configure','build','inventory','tests']+(['actor-tests'] if lane!='gcc' else [])
  for x in commands:assert x.get('exit')==0 and 'exception' not in x and s.sha(x['log'])==x['log_sha256']
  maps[lane]=s.load(root/'images-before.json');assert maps[lane]==s.load(root/'images-after.json') and len(maps[lane])==(18 if lane=='gcc' else 12)
  expected_targets={*suites.values(),batch.ACTOR,* (batch.PRODUCTS if lane=='gcc' else {})};assert set(maps[lane])==expected_targets==set(d['compiled_targets'])
  for image in maps[lane].values():assert s.sha(image['path'])==image['sha256'] and pathlib.Path(image['path']).stat().st_size==image['bytes']
  inventory=s.load(root/'inventory.log');registered={x['name']:x for x in inventory['tests'] if x['name'] in {*suites,batch.ACTOR_SUITE}}
  assert set(registered)=={*suites,batch.ACTOR_SUITE}
  for name,target in {**suites,batch.ACTOR_SUITE:batch.ACTOR}.items():assert pathlib.Path(registered[name]['command'][0]).resolve()==pathlib.Path(maps[lane][target]['path']).resolve(),name
  native_map=s.load(root/'native-source-map.json');assert native_map==snapshot['files_sha256']
  receipt=s.load(root/'build-receipt.json');assert receipt['source_manifest_sha256']==s.sha(root/'native-source-map.json') and receipt['command']==commands[1]['command'] and receipt['exit']==0
  references.append(s.load(root/'tool-inputs.json')['java_reference'])
  assert d['provider']==batch.provider_pins()
  if lane!='gcc':
   actor=d['actor_acceptance'];assert actor['status']=='PASS' and actor['actual_suites']==1 and actor['suite']==batch.ACTOR_SUITE and actor['image']==maps[lane][batch.ACTOR]
   assert s.sha(root/'actor-tests.xml')==actor['xml_sha256'] and s.sha(root/'actor-ctest-full.log')==actor['full_log_sha256'];s.passed_xml(root/'actor-tests.xml',[batch.ACTOR_SUITE])
   assert all(x.get('status')=='run' for x in ET.parse(root/'actor-tests.xml').getroot().iter('testcase'))
  else:assert d['actor_acceptance']=={'status':'COMPILE_ONLY_PRIVATE_RUN_PENDING','actual_suites':0,'image':maps[lane][batch.ACTOR]}
  blocks_by_lane[lane]=root
  inputs[lane]={'path':str(result_path),'sha256':s.sha(result_path)}
 assert all(x==references[0] for x in references)
 reference=references[0];assert {str(x.relative_to(reference['classes_directory'])):s.sha(x) for x in pathlib.Path(reference['classes_directory']).rglob('*.class')}==reference['classes']
 for group in ('java','jar'):
  for name,pin in reference[group].items():assert s.sha(name)==pin['sha256'] and pathlib.Path(name).stat().st_size==pin['bytes']
 def parse_blocks(log):
  blocks={};active=None
  for line in pathlib.Path(log).read_text().splitlines():
   match=re.fullmatch(r'\d+/\d+ Testing: (.+)',line)
   if match:active=match.group(1);assert active not in blocks;blocks[active]=[]
   elif active is not None:blocks[active].append(line)
  return blocks
 gcc=blocks_by_lane['gcc'];suites=batch.suites_for_lane('gcc');blocks=parse_blocks(gcc/'ctest-full.log');assert set(blocks)==set(suites)
 actor_blocks=[parse_blocks(blocks_by_lane[lane]/'actor-ctest-full.log') for lane in ('clang','san')]
 assert all(set(x)=={batch.ACTOR_SUITE} for x in actor_blocks)
 def qt_passes(lines):return [x.rsplit('::',1)[-1].removesuffix('()') for x in re.findall(r'^PASS\s*: (.+)$','\n'.join(lines),re.M)]
 actor_cases=qt_passes(actor_blocks[0][batch.ACTOR_SUITE]);assert len(actor_cases)==7 and actor_cases==qt_passes(actor_blocks[1][batch.ACTOR_SUITE])
 blocks[batch.ACTOR_SUITE]=actor_blocks[0][batch.ACTOR_SUITE]
 selectors={}
 for suite,target in suites.items():
  commands=[shlex.split(x.removeprefix('Command: ')) for x in blocks[suite] if x.startswith('Command: ')];assert len(commands)==1
  driver=pathlib.Path(maps['gcc'][target]['path']).resolve();positions=[i for i,part in enumerate(commands[0]) if pathlib.Path(part).is_absolute() and pathlib.Path(part).resolve()==driver];assert len(positions)==1;selectors[suite]=(commands[0],positions[0])
 actor_inventory=next(x for x in s.load(gcc/'inventory.log')['tests'] if x['name']==batch.ACTOR_SUITE);selectors[batch.ACTOR_SUITE]=(actor_inventory['command'],0)
 out=H/'private-runs'/a.attempt;out.mkdir(parents=True,exist_ok=False);env=s.environment('gcc')
 for key in ('CMAKE_BUILD_PARALLEL_LEVEL','ASAN_OPTIONS','LSAN_OPTIONS','UBSAN_OPTIONS','LD_DEBUG_OUTPUT','BIGLYBTC_PREVIEW_PROFILE','BIGLYBTC_HTTP_PRIVATE_RUNTIME','BIGLYBTC_I2P_STREAMING_ACCEPTANCE_ROOT','QT_TLS_BACKEND','QML_IMPORT_PATH','QML2_IMPORT_PATH','QT_QPA_PLATFORMTHEME'):env.pop(key,None)
 for key in ('HOME','XDG_CONFIG_HOME','XDG_CACHE_HOME','XDG_RUNTIME_DIR'):
  path=out/key.lower();path.mkdir(mode=0o700);env[key]=str(path)
 runner=s.Runner(out,env);result={'status':'FAIL','source_commit':source,'source_adoption_sha256':s.sha(a.adoption),'producer_sha256':s.sha(a.producer),'lane_inputs':inputs,'builds_executed':False,'provider_rebuilt':False,'historical_passes_pooled':False,'runner_sha256':s.sha(__file__)}
 try:
  plan=s.load(PLAN);provider=plan['provider'];artifact=pathlib.Path(provider['manifest']['path']);assert s.sha(artifact)==provider['manifest']['sha256']=='f746fd279755ea90f31e20b0c9131c46e5f0871c4aab3665f232975f98feb56e'
  actualprovider=pathlib.Path(s.load(artifact)['artifact']);assert s.sha(actualprovider)==provider['artifact_sha256']=='355f0c7bca7404c3199e85d4741a3734ac460c79d641a2539c049c5a8ad0c443'
  old=bundle(N/'dist',s);distpin=s.sha(N/'dist/BUILD_MANIFEST.json');candidate=out/'candidate';package=plan['debian_package'];assert s.sha(package['path'])==package['sha256']
  runner.run('stage',['python3','-B',N/'preview/migration/tools/stage_preview.py','--build',s.CACHES['gcc'],'--output',candidate,'--libtorrent-package',package['path'],'--source-libtorrent',actualprovider,'--private-mainline-routing-provider-manifest',artifact],timeout=240)
  staged=bundle(candidate,s);private=candidate/'usr/lib/biglybtc';proof=s.load(QT_PROOF);qt=[v for k,v in proof.items() if re.fullmatch(r'libQt6Test\.so\.6(?:\..+)?',pathlib.Path(k).name)];assert len(qt)==1 and s.sha(qt[0]['path'])==qt[0]['sha256']
  support=out/'test-support';support.mkdir();shutil.copyfile(qt[0]['path'],support/'libQt6Test.so.6')
  env.update(LD_LIBRARY_PATH=os.pathsep.join(map(str,[support,private/'lib',private/'qt/lib',candidate/'dependencies/libtorrent/usr/lib/x86_64-linux-gnu'])),QT_PLUGIN_PATH=str(private/'qt/plugins'),QT_QPA_PLATFORM_PLUGIN_PATH=str(private/'qt/plugins/platforms'))
  actual=[];copies={}
  for suite,target in {**suites,batch.ACTOR_SUITE:batch.ACTOR}.items():
   image=maps['gcc'][target];copied=out/target;shutil.copyfile(image['path'],copied);copied.chmod(0o755);assert s.sha(copied)==image['sha256'];copies[target]=image
   linked=runner.run('loader-'+suite,['ldd',copied],timeout=30).read_text();assert 'not found' not in linked and '/opt/Qt' not in linked
   expected=qt_passes(blocks[suite])
   command,index=selectors[suite];args=list(command);args[index]=str(copied)
   if expected:args+=['-o',str(out/(suite+'.txt'))+',txt','-o',str(out/(suite+'.xml'))+',junitxml']
   if suite==batch.ACTOR_SUITE:
    env['BIGLYBTC_I2P_STREAMING_ACCEPTANCE_ROOT']=str(out/'actual-java-peer')
    # Child JVM loader diagnostics go to their own PID files, preserving the
    # fixture's bounded stderr. Only the exact copied native image earns credit.
    env['LD_DEBUG_OUTPUT']=str(out/'actor-loader')
   env['LD_DEBUG']='libs'
   try:log=runner.run(suite,args,timeout=540 if suite==batch.ACTOR_SUITE else 180)
   finally:env.pop('LD_DEBUG',None);env.pop('LD_DEBUG_OUTPUT',None);env.pop('BIGLYBTC_I2P_STREAMING_ACCEPTANCE_ROOT',None)
   raw=log.read_text(errors='replace');assert not re.search(r'^(?:SKIP\s*:|FAIL!)',raw,re.M)
   native_trace=None
   if suite==batch.ACTOR_SUITE:
    traces=[x for x in out.glob('actor-loader.*') if re.search(r'initialize program:\s*'+re.escape(str(copied))+r'\s*$',x.read_text(errors='replace'),re.M)]
    assert len(traces)==1,'Exactly one actual copied actor loader trace required'
    native_trace=traces[0];raw=native_trace.read_text(errors='replace')
   assert re.search(r'initialize program:\s*'+re.escape(str(copied))+r'\s*$',raw,re.M),'Copied native driver was not actually initialised'
   loaded={pathlib.Path(x.strip()).resolve() for x in re.findall(r'calling init:\s*([^\n]+)',raw)}
   for name in re.findall(r'(libQt6[^ ]+|libtorrent-rasterbar[^ ]+) =>',linked):
    assert any(p.name==name or p.name.startswith(name+'.') for p in loaded),('Missing actual linked image',name)
   for imagepath in loaded:
    if imagepath.name.startswith(('libQt6','libqopensslbackend','libqoffscreen','libtorrent-rasterbar')):assert imagepath.is_relative_to(private.resolve()) or imagepath==(support/'libQt6Test.so.6').resolve()
   if re.search(r'libtorrent-rasterbar[^ ]+ =>',linked):assert any(x.name.startswith('libtorrent-rasterbar') and s.sha(x)==batch.PROVIDER_SHA for x in loaded)
   record={'suite':suite,'target':target,'exit':0,'arguments':list(map(str,args)),'log_sha256':s.sha(log),'initialized_images_sha256':{str(x):s.sha(x) for x in loaded if x.name.startswith(('libQt6','libtorrent-rasterbar','libssl.so','libcrypto.so'))}}
   if native_trace:record['native_loader_trace']={'path':str(native_trace),'sha256':s.sha(native_trace)}
   if expected:
    root=ET.parse(out/(suite+'.xml')).getroot();names=s.passed_xml(out/(suite+'.xml'));assert len(names)==len(expected) and {x.rsplit('::',1)[-1].removesuffix('()') for x in names}==set(expected)
    assert root.get('tests')==str(len(names)) and all(root.get(x)=='0' for x in ('failures','errors','skipped')) and all(x.get('status','run')=='run' for x in root.iter('testcase'))
    assert not re.search(r'^(?:SKIP\s*:|FAIL!)',(out/(suite+'.txt')).read_text(),re.M);record.update(case_names=names,xml_sha256=s.sha(out/(suite+'.xml')))
   actual.append(record)
  assert len(actual)==15 and len(copies)==15
  assert bundle(candidate,s)==staged and bundle(N/'dist',s)==old and s.sha(N/'dist/BUILD_MANIFEST.json')==distpin and s.source_snapshot(source)==snapshot
  for images in maps.values():
   for image in images.values():assert s.sha(image['path'])==image['sha256']
  assert batch.provider_pins()==s.load(a.gcc_result)['provider']
  assert {str(x.relative_to(reference['classes_directory'])):s.sha(x) for x in pathlib.Path(reference['classes_directory']).rglob('*.class')}==reference['classes']
  for group in ('java','jar'):
   for name,pin in reference[group].items():assert s.sha(name)==pin['sha256'] and pathlib.Path(name).stat().st_size==pin['bytes']
  result.update(status='PASS_PRIVATE_ROUTING_STREAMING',candidate=str(candidate),candidate_manifest_sha256=s.sha(candidate/'BUILD_MANIFEST.json'),actual_leaf_count=len(staged['files_sha256']),default_suites=14,separate_actor_suites=1,actual_tests=actual,copied_images=copies,old_dist_unchanged=distpin,source_unchanged=True,provider_sha256=s.sha(actualprovider),java_reference=reference)
 except BaseException as exc:result['failure']=repr(exc)
 finally:
  result['commands_sha256']=s.sha(out/'commands.json') if (out/'commands.json').exists() else None;s.write(out/'result.json',result)
 print(result['status'],out/'result.json',flush=True);return 0 if result['status'].startswith('PASS_') else 1
if __name__=='__main__':raise SystemExit(main())
