#!/usr/bin/env python3
"""Root-only six focused private commands; no build or subjects on import."""
import argparse,importlib.util,os,pathlib,re,shutil,xml.etree.ElementTree as ET
W=pathlib.Path(__file__).resolve().parent
A=W.parent
assert __import__('hashlib').sha256((A/'run.py').read_bytes()).hexdigest()=='3b7b8428207515ad75376af7647a11524e759d5cff6f173b56ac06696236decd'
spec=importlib.util.spec_from_file_location('associated_manager',A/'run.py');a1=importlib.util.module_from_spec(spec);spec.loader.exec_module(a1);b=a1.b;s=b.s;R=b.R;N=b.N
PLAN=R/'build/mainline-host-cohort-20261028/amendments/provider-r2-recipes/private/plan.json'
QT_PROOF=R/'build/mainline-host-cohort-20261028/amendments/provider-r2-recipes/qualification/runs/gcc-r4/runtime-images-before.json'
STATUS='PASS_FIVE_PRIVATE_SUBJECTS_PENDING_PUBLIC'
SELECTORS=[
 ('owned-manager','i2cp-bounded-stream-manager',None),
 ('actual-manager','i2p-original-streaming-manager',None),
 ('known-hints','azureus-routing','explicitKnownAttachmentUsesActualPingAndReplacesOnlyItsProtocolHint'),
 ('profile-pressure','azureus-profile-io','realNormalRepliesBackpressureWithinEightSlotsAndWithdrawOnSourceOrClose'),
 ('retained-legacy-http','i2p-original-streaming-peer','actualOriginalManagerCarriesHttpAndRecoverablePackets:http')]
def bundle(root):
 d=s.load(root/'BUILD_MANIFEST.json');assert d['distribution_scope']=='private-qualification-only'
 assert {str(p.relative_to(root)):s.sha(p) for p in root.rglob('*') if p.is_file() and p.name!='BUILD_MANIFEST.json'}==d['files_sha256'];return d

def expected_cases(block,selector):
 names=[x.rsplit('::',1)[-1].removesuffix('()') for x in re.findall(r'^PASS\s*: (.+)$',block,re.M)]
 assert len(names)==len(set(names)) and names
 if selector is None:return names
 method,sep,tag=selector.partition(':')
 selected=[name for name in names if name in ('initTestCase','cleanupTestCase') or (name==method+'('+tag+')' if sep else name==method or name.startswith(method+'('))]
 assert len(selected)>2;return selected

def verify_result(path,lane,source,adoption_path,reference_path):
 assert path.resolve()==(A/'runs'/(lane+'-a1')/'result.json').resolve()
 cap=s.load(W/'manifest.json')
 for name,pin in cap['artifacts_sha256'].items():assert s.sha(W/name)==pin
 for name,pin in cap['inputs_sha256'].items():assert s.sha(name)==pin
 a1.pins();r=s.load(path);root=path.parent;assert r['status']==('ASSOCIATED_CURRENT_M_BLOCKED_PUBLIC' if lane=='gcc' else 'PASS_ASSOCIATED_CURRENT_M_ROWS') and r['source_commit']==source and r['lane']==lane and r['runner_sha256']==s.sha(A/'run.py')
 assert r['qualification_mode']==a1.MODE and r['fresh_full_cohort'] is False and r['historical_different_source_passes_pooled'] is False and r['effective_adoption']==a1.ref(adoption_path) and r['java_preparation']==a1.ref(reference_path)
 for name,pin in r['artifacts_sha256'].items():assert s.sha(root/name)==pin
 current=a1.source_guard(source,s.load(adoption_path));assert s.load(root/'source-before.json')==s.load(root/'source-after.json')==current
 images=s.load(root/'images-before.json');assert images==s.load(root/'images-after.json')==s.images({name:pathlib.Path(row['path']) for name,row in images.items()})
 for leaf in ('tool-inputs.json','dependencies-before.json'):values=s.load(root/leaf);assert b.base.file_pins(values)==values
 commands=s.load(root/'commands.json');assert s.sha(root/'commands.json')==r['commands_sha256'] and [x['name'] for x in commands]==['configure','build','inventory','fresh-tests']
 for row in commands:assert row['exit']==0 and 'exception' not in row and s.sha(row['log'])==row['log_sha256']
 inventory={row['name']:row for row in s.load(root/'inventory.log')['tests'] if row['name'] in b.suites_for_lane(lane)}
 associations=a1.associate(lane,root/'fresh-tests.xml',images,inventory,current);assert associations==s.load(root/'associated-suites.json') and r['association']==a1.ref(root/'associated-suites.json')
 assert [name for name,row in associations.items() if row['outcome']=='BLOCKED']==(['azureus-public-bound'] if lane=='gcc' else []) and all(row['outcome'] in ('PASS','BLOCKED') for row in associations.values())
 assert b.peer_evidence(root)==r['peer_evidence'] and b.manager_evidence(a1.D2 if lane=='san' else root)==r['manager_evidence'] and b.protected_original()==r['original_head_index_unchanged'] and b.provider_pins()==r['provider']
 assert reference_path==a1.REFERENCE and a1.d.reference_pins()==s.load(root/'java-reference.json')
 original=s.load(a1.P/'runs/gcc-r2/images-before.json')
 if lane=='gcc':assert all(images[target]==original[target] for target in b.PRODUCTS)
 return dict(r,cache=str(s.CACHES[lane])),images,inventory

def main():
 p=argparse.ArgumentParser();p.add_argument('--attempt',required=True);p.add_argument('--source-commit',required=True)
 for name in ('adoption','reference','gcc-result','clang-result','san-result'):p.add_argument('--'+name,type=pathlib.Path,required=True)
 a=p.parse_args();assert re.fullmatch('[A-Za-z0-9_-]+',a.attempt);protected=b.protected_original();snapshot=a1.source_guard(a.source_commit,s.load(a.adoption));adoption=s.load(a.adoption);assert adoption['source_commit']==a.source_commit;a1.pins();b.prior_w(adoption['w_inputs'])
 results={};maps={};inventories={}
 for lane in ('gcc','clang','san'):
  path=getattr(a,lane+'_result');assert path.is_absolute();results[lane],maps[lane],inventories[lane]=verify_result(path,lane,a.source_commit,a.adoption,a.reference)
 blocks={};active=None
 for transcript in (a1.P/'runs/gcc-r2/ctest-full.log',a.gcc_result.parent/'fresh-ctest-full.log'):
  active=None
  for line in transcript.read_text().splitlines():
   found=re.fullmatch(r'\d+/\d+ Testing: (.+)',line)
   if found:active=found.group(1);blocks[active]=[]
   elif active is not None:blocks[active].append(line)
 assert set(blocks)==set(b.SUITES)
 out=W/'private-runs'/a.attempt;out.mkdir(parents=True,exist_ok=False);env=s.environment('gcc')
 for key in ('CMAKE_BUILD_PARALLEL_LEVEL','BIGLYBTC_HTTP_PRIVATE_RUNTIME','QML_IMPORT_PATH','QML2_IMPORT_PATH','QT_QPA_PLATFORMTHEME','ASAN_OPTIONS','LSAN_OPTIONS','UBSAN_OPTIONS','LD_DEBUG_OUTPUT','BIGLYBTC_PREVIEW_PROFILE','QT_TLS_BACKEND','JAVA_TOOL_OPTIONS','_JAVA_OPTIONS','JDK_JAVA_OPTIONS','CLASSPATH','BIGLYBTC_I2P_STREAMING_ACCEPTANCE_ROOT','BIGLYBTC_I2P_MANAGER_ACCEPTANCE_ROOT','BIGLYBTC_PUBLIC_ADMIN_MODE','BIGLYBTC_PUBLIC_ADMIN_HELPER','BIGLYBTC_PUBLIC_ADMIN_SHA256','BIGLYBTC_PUBLIC_ADMIN_RECEIPT'):env.pop(key,None)
 for key in ('HOME','XDG_CONFIG_HOME','XDG_CACHE_HOME','XDG_RUNTIME_DIR'):
  directory=out/key.lower();directory.mkdir(mode=0o700);env[key]=str(directory)
 runner=s.Runner(out,env);result={'status':'FAIL','source_commit':a.source_commit,'producer_sha256':s.sha(A/'run.py'),'source_adoption_sha256':s.sha(a.adoption),'reference_result_sha256':s.sha(a.reference),'runner_sha256':s.sha(__file__),'builds_executed':False,'provider_rebuilt':False,'historical_passes_pooled':False,'lane_inputs':{lane:{'path':str(getattr(a,lane+'_result')),'sha256':s.sha(getattr(a,lane+'_result'))} for lane in results}}
 try:
  plan=s.load(PLAN);provider=plan['provider'];binding=pathlib.Path(provider['manifest']['path']);assert s.sha(binding)==provider['manifest']['sha256']==b.base.BINDING_SHA
  artifact=pathlib.Path(s.load(binding)['artifact']);assert s.sha(artifact)==provider['artifact_sha256']==b.PROVIDER_SHA;package=plan['debian_package'];assert s.sha(package['path'])==package['sha256']
  old=bundle(N/'dist');oldpin=s.sha(N/'dist/BUILD_MANIFEST.json');candidate=out/'candidate'
  runner.run('stage',['python3','-B',N/'preview/migration/tools/stage_preview.py','--build',results['gcc']['cache'],'--output',candidate,'--libtorrent-package',package['path'],'--source-libtorrent',artifact,'--private-mainline-routing-provider-manifest',binding],timeout=240)
  staged=bundle(candidate);private=candidate/'usr/lib/biglybtc';proof=s.load(QT_PROOF);qt=[v for k,v in proof.items() if re.fullmatch(r'libQt6Test\.so\.6(?:\..+)?',pathlib.Path(k).name)];assert len(qt)==1 and s.sha(qt[0]['path'])==qt[0]['sha256']
  support=out/'test-support';support.mkdir();shutil.copyfile(qt[0]['path'],support/'libQt6Test.so.6')
  env.update(LD_LIBRARY_PATH=os.pathsep.join(map(str,[support,private/'lib',private/'qt/lib',candidate/'dependencies/libtorrent/usr/lib/x86_64-linux-gnu'])),QT_PLUGIN_PATH=str(private/'qt/plugins'),QT_QPA_PLATFORM_PLUGIN_PATH=str(private/'qt/plugins/platforms'))
  wrapper=out/'azureus_public_bound_namespace.py';shutil.copyfile(b.PUBLIC_WRAPPER,wrapper);wrapperpin={'path':str(wrapper),'sha256':s.sha(wrapper),'source_path':str(b.PUBLIC_WRAPPER)};assert wrapperpin['sha256']==s.sha(b.PUBLIC_WRAPPER)
  companion=out/b.PUBLIC_LAUNCHER.name;shutil.copyfile(b.PUBLIC_LAUNCHER,companion);companionpin={'path':str(companion),'sha256':s.sha(companion),'source_path':str(b.PUBLIC_LAUNCHER)};assert companionpin['sha256']==s.sha(b.PUBLIC_LAUNCHER)
  actual=[];copies={}
  for suite in [row[1] for row in SELECTORS]+['azureus-public-bound']:
   target=b.SUITES[suite];image=maps['gcc'][target];copied=out/target;shutil.copyfile(image['path'],copied);copied.chmod(0o755);assert s.sha(copied)==image['sha256'];copies[target]=image
  pending={'executed':False,'suite':'azureus-public-bound','selector':'explicitPublicKnownHintIsNotExportedAndOnlyActualPingMakesItAlive','image':copies[b.SUITES['azureus-public-bound']],'copied_path':str(out/b.SUITES['azureus-public-bound']),'wrapper':wrapperpin,'launcher':companionpin,'authentication':'pending; no authorisation or namespace invocation in this runner'}
  with b.subject_phase(out):
   for label,suite,selector in SELECTORS:
    target=b.SUITES[suite];image=maps['gcc'][target];copied=out/target
    if target not in copies:shutil.copyfile(image['path'],copied);copied.chmod(0o755);copies[target]=image
    assert s.sha(copied)==image['sha256'];linked=runner.run('loader-'+label,['ldd',copied],timeout=30).read_text();assert 'not found' not in linked and '/opt/Qt' not in linked
    selected=inventories['gcc'][suite];expected=expected_cases('\n'.join(blocks[suite]),selector)
    args=[str(copied),*([selector] if selector else selected['command'][1:])]
    if target==b.ACTOR:env['BIGLYBTC_I2P_STREAMING_ACCEPTANCE_ROOT']=str(out/'actual-java-peer')
    if target==b.MANAGER:env['BIGLYBTC_I2P_MANAGER_ACCEPTANCE_ROOT']=str(out/'actual-manager')
    args+=['-o',str(out/(label+'.txt'))+',txt','-o',str(out/(label+'.xml'))+',junitxml'];budget=float({x['name']:x['value'] for x in selected['properties']}['TIMEOUT']);assert 0<budget<=480
    prefix='native-loader-'+label;env.update(LD_DEBUG='libs',LD_DEBUG_OUTPUT=str(out/prefix))
    try:log=runner.run(label,args,timeout=budget+30)
    finally:
     for key in ('LD_DEBUG','LD_DEBUG_OUTPUT','BIGLYBTC_I2P_STREAMING_ACCEPTANCE_ROOT','BIGLYBTC_I2P_MANAGER_ACCEPTANCE_ROOT','BIGLYBTC_PUBLIC_ADMIN_MODE','BIGLYBTC_PUBLIC_ADMIN_HELPER','BIGLYBTC_PUBLIC_ADMIN_SHA256','BIGLYBTC_PUBLIC_ADMIN_RECEIPT'):env.pop(key,None)
    assert not re.search(r'^(?:SKIP\s*:|FAIL!)',log.read_text(errors='replace'),re.M)
    names=s.passed_xml(out/(label+'.xml'));assert len(names)==len(expected) and {x.rsplit('::',1)[-1].removesuffix('()') for x in names}==set(expected)
    xml=ET.parse(out/(label+'.xml')).getroot();assert xml.get('tests')==str(len(names)) and all(xml.get(key)=='0' for key in ('failures','errors','skipped')) and all(c.get('status','run')=='run' for c in xml.iter('testcase'))
    traces=[t for t in out.glob(prefix+'.*') if re.search(r'initialize program:\s*'+re.escape(str(copied))+r'\s*$',t.read_text(errors='replace'),re.M)];assert len(traces)==1;trace=traces[0];loaded={pathlib.Path(v.strip()).resolve() for v in re.findall(r'calling init:\s*([^\n]+)',trace.read_text(errors='replace'))}
    for name in re.findall(r'(libQt6[^ ]+|libtorrent-rasterbar[^ ]+) =>',linked):assert any(p.name==name or p.name.startswith(name+'.') for p in loaded)
    for path in loaded:
     if path.name.startswith(('libQt6','libqopensslbackend','libqoffscreen','libtorrent-rasterbar')):assert path.is_relative_to(private.resolve()) or path==(support/'libQt6Test.so.6').resolve()
    if re.search(r'libtorrent-rasterbar[^ ]+ =>',linked):assert any(path.name.startswith('libtorrent-rasterbar') and s.sha(path)==b.PROVIDER_SHA for path in loaded)
    actual.append({'label':label,'suite':suite,'target':target,'selector':selector,'exit':0,'arguments':args,'declared_timeout_seconds':budget,'case_names':names,'xml_sha256':s.sha(out/(label+'.xml')),'log_sha256':s.sha(log),'native_loader_trace':{'path':str(trace),'sha256':s.sha(trace)},'initialized_images_sha256':{str(path):s.sha(path) for path in loaded if path.name.startswith(('libQt6','libtorrent-rasterbar','libssl.so','libcrypto.so'))}})
  legacy=out/'actual-java-peer/http';legacyfiles={str(legacy/name):{'sha256':s.sha(legacy/name),'bytes':(legacy/name).stat().st_size} for name in ('java-command.json','java.stderr','peer-ready.json','peer-application.json','peer-result.json','bridge.json')}
  assert s.load(legacy/'peer-result.json')['status']==s.load(legacy/'peer-application.json')['status']=='PASS'
  assert len(actual)==5 and len(copies)==6 and s.sha(wrapper)==wrapperpin['sha256'] and s.sha(companion)==companionpin['sha256'] and bundle(candidate)==staged and bundle(N/'dist')==old and s.sha(N/'dist/BUILD_MANIFEST.json')==oldpin and b.source_snapshot(a.source_commit)==snapshot and b.protected_original()==protected
  for lane in results:verify_result(getattr(a,lane+'_result'),lane,a.source_commit,a.adoption,a.reference)
  result.update(status=STATUS,candidate=str(candidate),candidate_manifest_sha256=s.sha(candidate/'BUILD_MANIFEST.json'),actual_leaf_count=len(staged['files_sha256']),actual_tests=actual,copied_images=copies,namespace_wrapper=wrapperpin,namespace_launcher=companionpin,pending_public=pending,promotable=False,actual_subjects=5,source_unchanged=True,original_head_index_unchanged=protected,old_dist_unchanged=oldpin,provider_sha256=s.sha(artifact),subject_phase_sha256=s.sha(out/'subject-phase.json'),java_reference=a1.d.reference_pins(),manager_evidence=b.manager_evidence(out),retained_legacy_http={'subject_cases':1,'public_files':legacyfiles})
 except BaseException as exc:result['failure']=repr(exc)
 finally:result['commands_sha256']=s.sha(out/'commands.json') if (out/'commands.json').exists() else None;s.write(out/'result.json',result)
 print(result['status'],out/'result.json',flush=True);return 0 if result['status']==STATUS else 1
if __name__=='__main__':raise SystemExit(main())
