#!/usr/bin/env python3
"""Stage once and run the five bounded I2CP receiver suites with private libraries."""
import argparse,importlib.util,os,pathlib,re,shlex,shutil,xml.etree.ElementTree as ET
R=pathlib.Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';H=pathlib.Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('batch',H/'run-r2.py');batch=importlib.util.module_from_spec(spec);spec.loader.exec_module(batch);s=batch.s
PLAN=R/'build/mainline-host-cohort-20261028/amendments/provider-r2-recipes/private/plan.json'
QT_PROOF=R/'build/mainline-host-cohort-20261028/amendments/provider-r2-recipes/qualification/runs/gcc-r4/runtime-images-before.json'
def bundle(root):
 d=s.load(root/'BUILD_MANIFEST.json');assert d['distribution_scope']=='private-qualification-only'
 assert {str(p.relative_to(root)):s.sha(p) for p in root.rglob('*') if p.is_file() and p.name!='BUILD_MANIFEST.json'}==d['files_sha256'];return d

def main():
 p=argparse.ArgumentParser();p.add_argument('--attempt',required=True);p.add_argument('--lane-attempt',required=True);a=p.parse_args();assert all(re.fullmatch('[A-Za-z0-9_-]+',x) for x in (a.attempt,a.lane_attempt))
 adoption=s.load(H/'root-adoption-r2.json');source=adoption['source_commit'];snapshot=s.source_snapshot(source);maps={};inputs={}
 for lane in ('gcc','clang','san'):
  root=H/'runs'/(lane+'-'+a.lane_attempt);d=s.load(root/'result.json');assert d['status']=='PASS_BOUNDED_I2CP_RECEIVER_COHORT' and d['source_commit']==source and d['lane']==lane and d['actual_suites']==len(batch.suites_for_lane(lane))
  assert s.load(root/'source-before.json')==snapshot and d['runner_sha256']==s.sha(H/'run-r2.py') and d['source_receipt_sha256']==s.sha(H/'root-adoption-r2.json')
  assert s.sha(root/'tests.xml')==d['tests_xml_sha256'] and s.sha(root/'ctest-full.log')==d['full_ctest_log_sha256'];s.passed_xml(root/'tests.xml',batch.suites_for_lane(lane))
  assert all(c.get('status')=='run' for c in ET.parse(root/'tests.xml').getroot().iter('testcase'))
  assert s.sha(root/'commands.json')==d['commands_sha256'];commands=s.load(root/'commands.json');assert [x['name'] for x in commands]==['build','tests']
  for x in commands:assert x.get('exit')==0 and 'exception' not in x and s.sha(x['log'])==x['log_sha256']
  maps[lane]=s.load(root/'images-before.json');assert len(maps[lane])==(8 if lane=='gcc' else 5)
  for image in maps[lane].values():assert s.sha(image['path'])==image['sha256']
  inputs[lane]={'path':str(root/'result.json'),'sha256':s.sha(root/'result.json')}
 gcc=H/'runs'/('gcc-'+a.lane_attempt);blocks={};active=None
 for line in (gcc/'ctest-full.log').read_text().splitlines():
  match=re.fullmatch(r'\d+/\d+ Testing: (.+)',line)
  if match:active=match.group(1);assert active not in blocks;blocks[active]=[]
  elif active is not None:blocks[active].append(line)
 assert set(blocks)==set(batch.SUITES)
 selectors={}
 for suite,target in batch.SUITES.items():
  commands=[shlex.split(x.removeprefix('Command: ')) for x in blocks[suite] if x.startswith('Command: ')];assert len(commands)==1
  assert pathlib.Path(commands[0][0]).resolve()==pathlib.Path(maps['gcc'][target]['path']).resolve();selectors[suite]=commands[0][1:]
 out=H/'private-runs'/a.attempt;out.mkdir(parents=True,exist_ok=False);env=s.environment('gcc')
 for key in ('ASAN_OPTIONS','LSAN_OPTIONS','UBSAN_OPTIONS','LD_DEBUG_OUTPUT','BIGLYBTC_PREVIEW_PROFILE','BIGLYBTC_HTTP_PRIVATE_RUNTIME','QT_TLS_BACKEND','QML_IMPORT_PATH','QML2_IMPORT_PATH','QT_QPA_PLATFORMTHEME'):env.pop(key,None)
 for key in ('HOME','XDG_CONFIG_HOME','XDG_CACHE_HOME','XDG_RUNTIME_DIR'):
  path=out/key.lower();path.mkdir(mode=0o700);env[key]=str(path)
 runner=s.Runner(out,env);result={'status':'FAIL','source_commit':source,'lane_inputs':inputs,'builds_executed':False,'provider_rebuilt':False,'historical_passes_pooled':False,'runner_sha256':s.sha(__file__)}
 try:
  plan=s.load(PLAN);provider=plan['provider'];artifact=pathlib.Path(provider['manifest']['path']);assert s.sha(artifact)==provider['manifest']['sha256']=='f746fd279755ea90f31e20b0c9131c46e5f0871c4aab3665f232975f98feb56e'
  actualprovider=pathlib.Path(s.load(artifact)['artifact']);assert s.sha(actualprovider)==provider['artifact_sha256']=='355f0c7bca7404c3199e85d4741a3734ac460c79d641a2539c049c5a8ad0c443'
  old=bundle(N/'dist');distpin=s.sha(N/'dist/BUILD_MANIFEST.json');candidate=out/'candidate';package=plan['debian_package'];assert s.sha(package['path'])==package['sha256']
  runner.run('stage',['python3','-B',N/'preview/migration/tools/stage_preview.py','--build',s.CACHES['gcc'],'--output',candidate,'--libtorrent-package',package['path'],'--source-libtorrent',actualprovider,'--private-mainline-routing-provider-manifest',artifact],timeout=240)
  staged=bundle(candidate);private=candidate/'usr/lib/biglybtc';proof=s.load(QT_PROOF);qt=[v for k,v in proof.items() if re.fullmatch(r'libQt6Test\.so\.6(?:\..+)?',pathlib.Path(k).name)];assert len(qt)==1 and s.sha(qt[0]['path'])==qt[0]['sha256']
  support=out/'test-support';support.mkdir();shutil.copyfile(qt[0]['path'],support/'libQt6Test.so.6')
  env.update(LD_LIBRARY_PATH=os.pathsep.join(map(str,[support,private/'lib',private/'qt/lib',candidate/'dependencies/libtorrent/usr/lib/x86_64-linux-gnu'])),QT_PLUGIN_PATH=str(private/'qt/plugins'),QT_QPA_PLATFORM_PLUGIN_PATH=str(private/'qt/plugins/platforms'))
  actual=[];copies={}
  for suite,target in batch.SUITES.items():
   image=maps['gcc'][target];copied=out/target;shutil.copyfile(image['path'],copied);copied.chmod(0o755);assert s.sha(copied)==image['sha256'];copies[target]=image
   linked=runner.run('loader-'+suite,['ldd',copied],timeout=30).read_text();assert 'not found' not in linked and '/opt/Qt' not in linked
   expected=[x.rsplit('::',1)[-1].removesuffix('()') for x in re.findall(r'^PASS\s*: (.+)$','\n'.join(blocks[suite]),re.M)]
   args=[copied,*selectors[suite]]
   if expected:args+=['-o',str(out/(suite+'.txt'))+',txt','-o',str(out/(suite+'.xml'))+',junitxml']
   env['LD_DEBUG']='libs'
   try:log=runner.run(suite,args,timeout=180)
   finally:env.pop('LD_DEBUG',None)
   raw=log.read_text(errors='replace');assert not re.search(r'^(?:SKIP\s*:|FAIL!)',raw,re.M)
   loaded={pathlib.Path(x.strip()).resolve() for x in re.findall(r'calling init:\s*([^\n]+)',raw)}
   for name in re.findall(r'(libQt6[^ ]+|libtorrent-rasterbar[^ ]+) =>',linked):
    assert any(p.name==name or p.name.startswith(name+'.') for p in loaded),('Missing actual linked image',name)
   for imagepath in loaded:
    if imagepath.name.startswith(('libQt6','libqopensslbackend','libqoffscreen','libtorrent-rasterbar')):assert imagepath.is_relative_to(private.resolve()) or imagepath==(support/'libQt6Test.so.6').resolve()
   record={'suite':suite,'target':target,'exit':0,'arguments':list(map(str,args)),'log_sha256':s.sha(log),'initialized_images_sha256':{str(x):s.sha(x) for x in loaded if x.name.startswith(('libQt6','libtorrent-rasterbar','libssl.so','libcrypto.so'))}}
   if expected:
    root=ET.parse(out/(suite+'.xml')).getroot();names=s.passed_xml(out/(suite+'.xml'));assert len(names)==len(expected) and {x.rsplit('::',1)[-1].removesuffix('()') for x in names}==set(expected)
    assert root.get('tests')==str(len(names)) and all(root.get(x)=='0' for x in ('failures','errors','skipped')) and all(x.get('status','run')=='run' for x in root.iter('testcase'))
    assert not re.search(r'^(?:SKIP\s*:|FAIL!)',(out/(suite+'.txt')).read_text(),re.M);record.update(case_names=names,xml_sha256=s.sha(out/(suite+'.xml')))
   actual.append(record)
  assert bundle(candidate)==staged and bundle(N/'dist')==old and s.sha(N/'dist/BUILD_MANIFEST.json')==distpin and s.source_snapshot(source)==snapshot
  for images in maps.values():
   for image in images.values():assert s.sha(image['path'])==image['sha256']
  result.update(status='PASS_PRIVATE_BOUNDED_I2CP_RECEIVER',candidate=str(candidate),candidate_manifest_sha256=s.sha(candidate/'BUILD_MANIFEST.json'),actual_leaf_count=len(staged['files_sha256']),actual_tests=actual,copied_images=copies,old_dist_unchanged=distpin,source_unchanged=True,provider_sha256=s.sha(actualprovider))
 except BaseException as exc:result['failure']=repr(exc)
 finally:
  result['commands_sha256']=s.sha(out/'commands.json') if (out/'commands.json').exists() else None;s.write(out/'result.json',result)
 print(result['status'],out/'result.json',flush=True);return 0 if result['status'].startswith('PASS_') else 1
if __name__=='__main__':raise SystemExit(main())
