#!/usr/bin/env python3
"""Root-only sixth private subject; immutable five-subject origin, no build/stage."""
import argparse,hashlib,importlib.util,os,pathlib,re,xml.etree.ElementTree as ET
W=pathlib.Path(__file__).resolve().parent;P=W.parent/'private-five'
assert hashlib.sha256((P/'private.py').read_bytes()).hexdigest()=='8f953be1181e406f7193f1f48ce399b89f2765ee8dab88e44a49629aa6e73540'
spec=importlib.util.spec_from_file_location('private_five',P/'private.py');p=importlib.util.module_from_spec(spec);spec.loader.exec_module(p);a1=p.a1;b=p.b;s=p.s;N=p.N
STATUS='PASS_SIX_PRIVATE_SUBJECTS_RESUMED_PUBLIC'
SELECTOR='explicitPublicKnownHintIsNotExportedAndOnlyActualPingMakesItAlive'
def ref(path):return {'path':str(path),'sha256':s.sha(path)}
def five_origin(path,source):
 value=s.load(path);root=path.parent;assert path.resolve().is_relative_to((P/'private-runs').resolve()) and path.name=='result.json'
 assert value['status']==p.STATUS and value['source_commit']==source and value['runner_sha256']==s.sha(P/'private.py') and value['producer_sha256']==s.sha(a1.W/'run.py')
 assert value['actual_subjects']==5 and value['promotable'] is False and value['builds_executed'] is False and value['provider_rebuilt'] is False and value['historical_passes_pooled'] is False
 assert value['source_adoption_sha256']==s.sha(a1.ADOPTION) and value['reference_result_sha256']==s.sha(a1.REFERENCE) and value['java_reference']==a1.reference_pins(source)
 for label,suite,selector in p.SELECTORS:
  row=next(x for x in value['actual_tests'] if x['label']==label);assert row['suite']==suite and row['selector']==selector and row['exit']==0
  assert s.sha(root/(label+'.xml'))==row['xml_sha256'] and s.sha(root/(label+'.log'))==row['log_sha256'] and s.passed_xml(root/(label+'.xml'))==row['case_names']
  assert s.sha(row['native_loader_trace']['path'])==row['native_loader_trace']['sha256']
  for name,pin in row['initialized_images_sha256'].items():assert s.sha(name)==pin
 assert len(value['actual_tests'])==5 and s.sha(root/'commands.json')==value['commands_sha256']
 for row in s.load(root/'commands.json'):assert row['exit']==0 and 'exception' not in row and s.sha(row['log'])==row['log_sha256']
 assert value['pending_public']['executed'] is False and value['pending_public']['selector']==SELECTOR
 assert value['namespace_wrapper']==value['pending_public']['wrapper'] and value['namespace_launcher']==value['pending_public']['launcher']
 for name,original in (('namespace_wrapper',b.PUBLIC_WRAPPER),('namespace_launcher',b.PUBLIC_LAUNCHER)):
  row=value[name];assert s.sha(row['path'])==row['sha256']==s.sha(original)
 candidate=pathlib.Path(value['candidate']);assert candidate==root/'candidate' and s.sha(candidate/'BUILD_MANIFEST.json')==value['candidate_manifest_sha256'];p.bundle(candidate)
 assert s.sha(N/'dist/BUILD_MANIFEST.json')==value['old_dist_unchanged'] and b.protected_original()==value['original_head_index_unchanged']
 return value

def main():
 parser=argparse.ArgumentParser();parser.add_argument('--attempt',required=True);parser.add_argument('--source-commit',required=True);parser.add_argument('--private-five-result',type=pathlib.Path,required=True);a=parser.parse_args();assert re.fullmatch('[A-Za-z0-9_-]+',a.attempt)
 cap=s.load(W/'manifest.json')
 for name,pin in cap['artifacts_sha256'].items():assert s.sha(W/name)==pin
 for name,pin in cap['inputs_sha256'].items():assert s.sha(name)==pin
 adoption=s.load(a1.ADOPTION);snapshot=a1.source_guard(a.source_commit,adoption);reference=a1.reference_pins(a.source_commit);protected=b.protected_original();origin=ref(a.private_five_result);old=five_origin(a.private_five_result,a.source_commit);oldroot=a.private_five_result.parent;candidate=pathlib.Path(old['candidate']);staged=p.bundle(candidate)
 maps={};inventories={}
 for lane in ('gcc','clang','san'):
  receipt=old['lane_inputs'][lane];path=pathlib.Path(receipt['path']);assert s.sha(path)==receipt['sha256'];_,maps[lane],inventories[lane]=p.verify_result(path,lane,a.source_commit,a1.ADOPTION,a1.REFERENCE)
 for target,image in old['copied_images'].items():assert maps['gcc'][target]==image and s.sha(oldroot/target)==image['sha256']
 assert len(old['copied_images'])==6
 image=old['copied_images'][b.SUITES['azureus-public-bound']];copied=oldroot/b.SUITES['azureus-public-bound'];assert old['pending_public']['image']==image and old['pending_public']['copied_path']==str(copied)
 out=W/'runs'/a.attempt;out.mkdir(parents=True,exist_ok=False);env=s.environment('gcc')
 for key in ('CMAKE_BUILD_PARALLEL_LEVEL','BIGLYBTC_HTTP_PRIVATE_RUNTIME','QML_IMPORT_PATH','QML2_IMPORT_PATH','QT_QPA_PLATFORMTHEME','ASAN_OPTIONS','LSAN_OPTIONS','UBSAN_OPTIONS','LD_DEBUG_OUTPUT','BIGLYBTC_PREVIEW_PROFILE','QT_TLS_BACKEND','JAVA_TOOL_OPTIONS','_JAVA_OPTIONS','JDK_JAVA_OPTIONS','CLASSPATH','BIGLYBTC_I2P_STREAMING_ACCEPTANCE_ROOT','BIGLYBTC_I2P_MANAGER_ACCEPTANCE_ROOT','BIGLYBTC_PUBLIC_ADMIN_MODE','BIGLYBTC_PUBLIC_ADMIN_HELPER','BIGLYBTC_PUBLIC_ADMIN_SHA256','BIGLYBTC_PUBLIC_ADMIN_RECEIPT'):env.pop(key,None)
 for key in ('HOME','XDG_CONFIG_HOME','XDG_CACHE_HOME','XDG_RUNTIME_DIR'):directory=out/key.lower();directory.mkdir(mode=0o700);env[key]=str(directory)
 private=candidate/'usr/lib/biglybtc';support=oldroot/'test-support';env.update(LD_LIBRARY_PATH=os.pathsep.join(map(str,[support,private/'lib',private/'qt/lib',candidate/'dependencies/libtorrent/usr/lib/x86_64-linux-gnu'])),QT_PLUGIN_PATH=str(private/'qt/plugins'),QT_QPA_PLATFORM_PLUGIN_PATH=str(private/'qt/plugins/platforms'))
 runner=s.Runner(out,env);result={'status':'FAIL','source_commit':a.source_commit,'runner_sha256':s.sha(__file__),'private_five_origin':origin,'builds_executed':False,'stage_executed':False,'new_javac_invocations':0,'fresh_subjects':1,'retained_private_subjects':5,'promotable':False,'cached_gcc_public_required_separately':True}
 try:
  linked=runner.run('loader-public-known',['ldd',copied],timeout=30).read_text();assert 'not found' not in linked and '/opt/Qt' not in linked
  wrapper=pathlib.Path(old['namespace_wrapper']['path']);launcher=pathlib.Path(old['namespace_launcher']['path']);selected=inventories['gcc']['azureus-public-bound'];assert pathlib.Path(selected['command'][1]).resolve()==b.PUBLIC_WRAPPER.resolve()
  args=[selected['command'][0],str(wrapper),str(copied),SELECTOR,'-o',str(out/'public-known.txt')+',txt','-o',str(out/'public-known.xml')+',junitxml'];budget=float({x['name']:x['value'] for x in selected['properties']}['TIMEOUT']);assert budget==1000
  env.update(b.namespace_environment(out,adoption));prefix='native-loader-public-known';env.update(LD_DEBUG='libs',LD_DEBUG_OUTPUT=str(out/prefix))
  with b.subject_phase(out):
   try:log=runner.run('public-known',args,timeout=budget+30)
   finally:
    for key in ('LD_DEBUG','LD_DEBUG_OUTPUT','BIGLYBTC_PUBLIC_ADMIN_MODE','BIGLYBTC_PUBLIC_ADMIN_HELPER','BIGLYBTC_PUBLIC_ADMIN_SHA256','BIGLYBTC_PUBLIC_ADMIN_RECEIPT'):env.pop(key,None)
  namespace_result=b.namespace_receipt(out/'namespace-admin.json',b.public_pins(adoption)['namespace_admin'],args[2:],launcher)
  names=s.passed_xml(out/'public-known.xml');assert len(names)==3 and {x.rsplit('::',1)[-1].removesuffix('()') for x in names}=={'initTestCase',SELECTOR,'cleanupTestCase'}
  xml=ET.parse(out/'public-known.xml').getroot();assert xml.get('tests')=='3' and all(xml.get(k)=='0' for k in ('failures','errors','skipped')) and all(c.get('status','run')=='run' for c in xml.iter('testcase'))
  assert not re.search(r'^(?:SKIP\s*:|FAIL!)',log.read_text(errors='replace'),re.M)
  traces=[t for t in out.glob(prefix+'.*') if re.search(r'initialize program:\s*'+re.escape(str(copied))+r'\s*$',t.read_text(errors='replace'),re.M)];assert len(traces)==1;trace=traces[0];loaded={pathlib.Path(v.strip()).resolve() for v in re.findall(r'calling init:\s*([^\n]+)',trace.read_text(errors='replace'))}
  for name in re.findall(r'(libQt6[^ ]+|libtorrent-rasterbar[^ ]+) =>',linked):assert any(p.name==name or p.name.startswith(name+'.') for p in loaded)
  for path in loaded:
   if path.name.startswith(('libQt6','libqopensslbackend','libqoffscreen','libtorrent-rasterbar')):assert path.is_relative_to(private.resolve()) or path==(support/'libQt6Test.so.6').resolve()
  if re.search(r'libtorrent-rasterbar[^ ]+ =>',linked):assert any(path.name.startswith('libtorrent-rasterbar') and s.sha(path)==b.PROVIDER_SHA for path in loaded)
  actual={'label':'public-known','suite':'azureus-public-bound','target':b.SUITES['azureus-public-bound'],'selector':SELECTOR,'exit':0,'arguments':args,'declared_timeout_seconds':budget,'case_names':names,'xml_sha256':s.sha(out/'public-known.xml'),'log_sha256':s.sha(log),'native_loader_trace':ref(trace),'initialized_images_sha256':{str(path):s.sha(path) for path in loaded if path.name.startswith(('libQt6','libtorrent-rasterbar','libssl.so','libcrypto.so'))}}
  assert ref(a.private_five_result)==origin and five_origin(a.private_five_result,a.source_commit)==old and p.bundle(candidate)==staged and a1.source_guard(a.source_commit,adoption)==snapshot and a1.reference_pins(a.source_commit)==reference and b.protected_original()==protected
  for lane in maps:p.verify_result(pathlib.Path(old['lane_inputs'][lane]['path']),lane,a.source_commit,a1.ADOPTION,a1.REFERENCE)
  result.update(status=STATUS,actual_subjects=6,actual_tests=[*old['actual_tests'],actual],fresh_public=actual,namespace_admin=b.current_namespace(out,b.public_pins(adoption)['namespace_admin'],args[2:],launcher),namespace_admin_receipt=namespace_result,candidate=str(candidate),candidate_manifest_sha256=old['candidate_manifest_sha256'],actual_leaf_count=old['actual_leaf_count'],copied_images=old['copied_images'],namespace_wrapper=old['namespace_wrapper'],namespace_launcher=old['namespace_launcher'],lane_inputs=old['lane_inputs'],java_reference=reference,source_unchanged=True,original_head_index_unchanged=protected,old_dist_unchanged=old['old_dist_unchanged'])
 except BaseException as exc:result['failure']=repr(exc)
 finally:result['commands_sha256']=s.sha(out/'commands.json') if (out/'commands.json').exists() else None;result['artifacts_sha256']={str(p.relative_to(out)):s.sha(p) for p in out.rglob('*') if p.is_file()};s.write(out/'result.json',result)
 print(result['status'],out/'result.json',flush=True);return 0 if result['status']==STATUS else 1
if __name__=='__main__':raise SystemExit(main())
