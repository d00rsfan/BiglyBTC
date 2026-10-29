#!/usr/bin/env python3
"""Root-run recovery of only the two fixed pre-subject Public auth waits."""
import argparse,copy,hashlib,importlib.util,json,os,pathlib,re,shutil,xml.etree.ElementTree as ET
W=pathlib.Path(__file__).resolve().parent
SPEC=importlib.util.spec_from_file_location('original_w',W/'run-admin.py');original=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(original)
s,R,N=original.s,original.R,original.N
SOURCE='04b6287a38cec6bd123e2648ada278ea348b85db'
OBSTRUCTION=W/'root-auth-obstruction-admin-r1.json';OBSTRUCTION_SHA='cb0c560e0699e3ec13c7e86a434ee39acd7c742bc2fa8fcbfa9cbb45c59a8f18'
CONTINUITY=W/'root-cache-continuity-admin-r1.json';CONTINUITY_SHA='2353d4c458e205d31459a86435ab139e411ae1503a25aad869cb91ff3d675f1a'
RECIPES=W/'corrections/auth-wait-recovery/manifest.json'
PUBLIC_SUITE='azureus-public-bound'
STATUS='PASS_RESUMED_SAME_COHORT_PUBLIC'
MODE='resumed_same_source_current_cohort'

def __getattr__(name):return getattr(original,name)
def pin(path):
 path=pathlib.Path(path);assert path.is_absolute() and path.is_file() and not path.is_symlink()
 return {'path':str(path),'sha256':s.sha(path),'bytes':path.stat().st_size}
def ref(value):
 assert pin(value['path'])==value;return s.load(value['path'])
def frozen_recipes():
 d=s.load(RECIPES)
 for file,expected in d['recipe_files_sha256'].items():assert s.sha(file)==expected
 for file,expected in d['inputs_sha256'].items():assert s.sha(file)==expected
 for name,expected in d['artifacts_sha256'].items():assert s.sha(RECIPES.parent/name)==expected
 return s.sha(RECIPES)
def records():
 assert s.sha(OBSTRUCTION)==OBSTRUCTION_SHA and s.sha(CONTINUITY)==CONTINUITY_SHA
 obstruction=s.load(OBSTRUCTION);continuity=s.load(CONTINUITY)
 assert obstruction['status']=='ROOT_REVIEWED_PRE_SUBJECT_AUTH_OBSTRUCTION' and obstruction['source_commit']==SOURCE
 assert obstruction['make_session']['joined'] is True and obstruction['make_session']['exit']==2 and obstruction['public_functional_credit_from_observation'] is False
 observation=ref(obstruction['process_observation']);assert observation['observation']=='OWNED_CURRENT_COHORT_PROCESS_SNAPSHOT_ONLY'
 assert observation['previous_sanitizer_observation']['kind']=='ROOT_REPORTED_PREVIOUS_DIRECT_PROC_INSPECTION'
 assert 'not a replayable process snapshot' in observation['previous_sanitizer_observation']['limitation']
 gcc=obstruction['lanes']['gcc'];processes=observation['processes'];wrapper=[p for p in processes if p['argv'][:2]==['/usr/bin/python3',str(original.PUBLIC_WRAPPER)]]
 assert len(wrapper)==1 and wrapper[0]['argv'][2]==str(s.CACHES['gcc']/'tests'/original.SUITES[PUBLIC_SUITE])
 auth=[p for p in processes if p['argv'][:2]==['/usr/bin/pkexec','--disable-internal-agent']]
 assert len(auth)==1 and auth[0]['status']['PPid']==str(wrapper[0]['pid']) and auth[0]['status']['State'].startswith('S ')
 assert not any(p['argv'][0]==wrapper[0]['argv'][2] for p in processes)
 assert continuity['status']=='ROOT_UNCHANGED_CURRENT_COHORT_CACHE_ATTESTATION' and continuity['source_commit']==SOURCE and continuity['make35342_joined'] is True
 assert continuity['ordinary_source_clean'] is True and continuity['subsequent_cache_mutations_or_compilers'] is False and continuity['original_failed_run_cache_hash_finalized'] is False
 for lane in ('gcc','clang','san'):assert pin(s.CACHES[lane]/'CMakeCache.txt')==continuity['current_cache_pins'][lane]
 return obstruction,continuity

def xml_rows(path,suites,failed=False):
 root=ET.parse(path).getroot();rows=list(root.iter('testcase'));assert len(rows)==len(suites) and len({r.get('name') for r in rows})==len(rows)
 assert {r.get('name') for r in rows}==set(suites)
 for row in rows:
  if failed and row.get('name')==PUBLIC_SUITE:
   assert row.get('status')=='fail' and len(row.findall('failure'))==1 and row.find('failure').attrib=={'message':'Timeout'}
   assert row.find('error') is None and row.find('skipped') is None and float(row.get('time'))>=240
   assert not (row.findtext('system-out') or '').strip()
  else:assert row.get('status')=='run' and not any(row.find(tag) is not None for tag in ('failure','error','skipped'))
 return root,{row.get('name'):row for row in rows}

def common_origin(lane,source,adoption_path,reference_path,namespace_preflight):
 assert source==SOURCE;obstruction,continuity=records();fixed=obstruction['lanes'][lane];path=pathlib.Path(fixed['result']['path']);d=ref(fixed['result']);root=path.parent
 assert d['source_commit']==SOURCE and d['lane']==lane and pathlib.Path(d['cache'])==s.CACHES[lane]
 assert d['runner_sha256']==s.sha(W/'run-admin.py') and d['source_receipt_sha256']==s.sha(adoption_path) and d['reference_result_sha256']==s.sha(reference_path)
 suites=original.suites_for_lane(lane);before=s.load(root/'source-before.json');assert original.source_snapshot(SOURCE,adoption_path)==before and s.load(root/'native-source-map.json')==before['files_sha256']
 assert before['source_commit']==SOURCE
 images=s.load(root/'images-before.json');expected=set(suites.values())|{original.FIXTURE_HELPER}|(set(original.PRODUCTS) if lane=='gcc' else set())
 assert set(images)==expected==set(d['compiled_targets'])
 for name,image in images.items():assert pin(image['path'])==image and pathlib.Path(image['path']).is_relative_to(s.CACHES[lane])
 assert d['provider']==original.provider_pins() and d['public_bound_inputs']==original.public_pins(s.load(adoption_path))
 tools=s.load(root/'tool-inputs.json')['files'];assert original.base.file_pins(tools)==tools
 deps=s.load(root/'dependencies-before.json');assert original.base.file_pins(deps)==deps
 compiler=d['c_fixture_compiler'];assert pin(compiler['path'])==compiler
 cache=original.base.cache_values(s.CACHES[lane]);assert cache['CMAKE_GENERATOR']=='Ninja' and pathlib.Path(cache['CMAKE_HOME_DIRECTORY']).resolve()==(N/'preview/native').resolve()
 assert pathlib.Path(cache['CMAKE_C_COMPILER']).resolve()==pathlib.Path(compiler['path'])
 for key in ('CMAKE_CXX_COMPILER','CMAKE_MAKE_PROGRAM'):assert str(pathlib.Path(cache[key]).resolve()) in tools
 assert d['fixture_linkage']==original.fixture_linkage(pathlib.Path(images[original.FIXTURE_HELPER]['path']))
 assert s.sha(root/'commands.json')==d['commands_sha256'];commands=s.load(root/'commands.json');assert [r['name'] for r in commands]==['configure','build','inventory','tests']
 for row in commands:
  assert 'exception' not in row and s.sha(row['log'])==row['log_sha256']
  assert row['exit']==(8 if row['name']=='tests' and lane!='clang' else 0)
 receipt=s.load(root/'build-receipt.json');assert receipt['exit']==0 and receipt['command']==commands[1]['command'] and receipt['source_manifest_sha256']==s.sha(root/'native-source-map.json')
 assert commands[-1]['command'][:4]==['nice','-n','5','ctest'] and '--stop-on-failure' not in commands[-1]['command']
 for key in ('junit','transcript','namespace_launch'):assert pin(fixed[key]['path'])==fixed[key]
 assert pathlib.Path(fixed['junit']['path'])==root/'tests.xml' and pathlib.Path(fixed['transcript']['path'])==root/'tests.log'
 inventory={x['name']:x for x in s.load(root/'inventory.log')['tests'] if x['name'] in suites};original.check_inventory(inventory,images,suites)
 assert fixed['total']==len(suites) and fixed['passed']==len(suites)-(lane!='clang') and fixed['failed']==([] if lane=='clang' else [PUBLIC_SUITE])
 xml_rows(root/'tests.xml',suites,failed=lane!='clang')
 retained=blocks(root/'ctest-full.log');assert set(retained)==set(suites)
 for name,lines in retained.items():
  if name!=PUBLIC_SUITE or lane=='clang':assert not re.search(r'^(?:SKIP\s*:|FAIL!)',''.join(lines),re.M)
 if lane=='clang':
  assert d['status']==original.STATUS
  original.verify_result(path,lane,source,adoption_path,reference_path,namespace_preflight)
 else:
  assert d['status']=='FAIL' and d['failure']=="AssertionError('tests')"
  assert fixed['namespace_launch']['bytes']==0 and pathlib.Path(fixed['namespace_launch']['path']).read_bytes()==b''
  assert not any((root/name).exists() for name in ('source-after.json','images-after.json'))
 assert original.reference_pins(reference_path)==s.load(root/'java-reference.json')
 original.namespace_pins(namespace_preflight)
 return path,d,images,inventory

def origin_refs(root):
 return {name:pin(root/name) for name in ('result.json','commands.json','tests.xml','tests.log','ctest-full.log','source-before.json','native-source-map.json','images-before.json','dependencies-before.json','tool-inputs.json','build-receipt.json','inventory.log','subject-phase.json','public-namespace-launch.json')}

def blocks(path):
 result={};current=None
 for line in pathlib.Path(path).read_text().splitlines(True):
  match=re.fullmatch(r'\d+/\d+ Testing: (.+)\n?',line)
  if match:current=match.group(1);assert current not in result;result[current]=[line]
  elif current is not None:result[current].append(line)
 return result


def associated_transcript(out,origin):
 old=blocks(origin/'ctest-full.log');new=blocks(out/'ctest-full.log');assert set(new)=={PUBLIC_SUITE}
 return '# DERIVED SAME-COHORT ASSOCIATION; not an actual full CTest invocation.\n'+''.join(''.join(new[PUBLIC_SUITE] if name==PUBLIC_SUITE else lines) for name,lines in old.items())

def associated_views(out,origin):
 old_root,old=xml_rows(origin/'tests.xml',original.suites_for_lane(s.load(origin/'result.json')['lane']),failed=True)
 new_root,new=xml_rows(out/'tests.xml',[PUBLIC_SUITE]);view=copy.deepcopy(old_root)
 parent=next(node for node in view.iter() if any(child.tag=='testcase' for child in node))
 for i,node in enumerate(parent):
  if node.tag=='testcase' and node.get('name')==PUBLIC_SUITE:parent.remove(node);parent.insert(i,copy.deepcopy(new[PUBLIC_SUITE]));break
 for node in view.iter():
  if node.tag in ('testsuite','testsuites'):
   rows=list(node.iter('testcase'));node.set('tests',str(len(rows)))
   for key in ('failures','errors','skipped'):node.set(key,'0')
   node.set('time',str(sum(float(row.get('time','0')) for row in rows)))
 ET.indent(view);ET.ElementTree(view).write(out/'associated-tests.xml',encoding='utf-8',xml_declaration=True)
 oldblocks=blocks(origin/'ctest-full.log');newblocks=blocks(out/'ctest-full.log');assert set(oldblocks)==set(old) and set(newblocks)=={PUBLIC_SUITE}
 (out/'associated-ctest.log').write_text(associated_transcript(out,origin))
 return {'kind':'derived current-cohort per-suite association, not a full rerun','junit':pin(out/'associated-tests.xml'),'transcript':pin(out/'associated-ctest.log'),'origin_passed_suites':sorted(set(old)-{PUBLIC_SUITE}),'fresh_suites':[PUBLIC_SUITE]}

def verify_candidate(result,out,lane,source,adoption_path,reference_path,namespace_preflight):
 frozen_recipes()
 origin,d,images,inventory=common_origin(lane,source,adoption_path,reference_path,namespace_preflight)
 assert lane in ('gcc','san')
 assert result['status']==STATUS and result['qualification_mode']==MODE and result['source_commit']==SOURCE and result['lane']==lane and result['original_failure_preserved'] is True
 assert result['runner_sha256']==s.sha(__file__) and result['recovery_manifest_sha256']==s.sha(RECIPES)
 assert result['origin_refs']==origin_refs(origin.parent)
 assert result['obstruction']==pin(OBSTRUCTION) and result['cache_continuity']==pin(CONTINUITY)
 assert result['source_receipt_sha256']==s.sha(adoption_path) and result['reference_result_sha256']==s.sha(reference_path)
 assert result['namespace_admin']=={**original.namespace_pins(namespace_preflight),'launch_environment':original.ng.environment(original.namespace_pins(namespace_preflight),out/'public-namespace-launch.json'),'launch_receipt':original.ng.launch_receipt(out/'public-namespace-launch.json',original.namespace_pins(namespace_preflight),inventory[PUBLIC_SUITE]['command'][2:],original.ng.environment(original.namespace_pins(namespace_preflight),out/'public-namespace-launch.json'),original.PUBLIC_LAUNCHER)}
 assert result['source_before']==result['source_after']==s.load(origin.parent/'source-before.json') and result['images_before']==result['images_after']==images
 assert result['current_cache_before']==result['current_cache_after']==pin(s.CACHES[lane]/'CMakeCache.txt')
 assert result['passed_origin_suites']==sorted(set(original.suites_for_lane(lane))-{PUBLIC_SUITE}) and result['fresh_suites']==[PUBLIC_SUITE] and result['actual_suites']==len(original.suites_for_lane(lane))
 assert set(result['suite_names'])==set(original.suites_for_lane(lane)) and result['historical_different_source_passes_pooled'] is False and result['full_invocation_repeated'] is False
 assert result['peer_evidence']==original.peer_evidence(origin.parent)
 for name,p in result['origin_refs'].items():assert pin(p['path'])==p
 assert result['subject_phase_sha256']==s.sha(out/'subject-phase.json');phase=s.load(out/'subject-phase.json');assert phase['scope']=='subject phase only' and phase['lock_path']==str(W/'subject-phase.lock') and phase['left_monotonic_ns']>=phase['entered_monotonic_ns']
 assert not re.search(r'^(?:SKIP\s*:|FAIL!)',(out/'ctest-full.log').read_text(errors='replace'),re.M)
 assert result['commands_sha256']==s.sha(out/'commands.json');commands=s.load(out/'commands.json');assert len(commands)==1 and commands[0]['name']=='public-tests' and commands[0]['exit']==0 and 'exception' not in commands[0] and s.sha(commands[0]['log'])==commands[0]['log_sha256']
 assert commands[0]['command']==['nice','-n','5','ctest','--test-dir',str(s.CACHES[lane]),'--parallel','1','-R','^azureus-public-bound$','--output-on-failure','--output-junit',str(out/'tests.xml')]
 xml_rows(out/'tests.xml',[PUBLIC_SUITE]);assert result['fresh_junit']==pin(out/'tests.xml') and result['fresh_transcript']==pin(out/'ctest-full.log')
 assert (out/'associated-ctest.log').read_text()==associated_transcript(out,origin.parent)
 original.base.check_xml(out/'associated-tests.xml',original.suites_for_lane(lane))
 assert result['association']['origin_passed_suites']==result['passed_origin_suites'] and result['association']['fresh_suites']==[PUBLIC_SUITE]
 assert result['association']['junit']==pin(out/'associated-tests.xml') and result['association']['transcript']==pin(out/'associated-ctest.log')
 for name,row in xml_rows(out/'associated-tests.xml',original.suites_for_lane(lane))[1].items():
  actual=xml_rows(out/'tests.xml',[PUBLIC_SUITE])[1][PUBLIC_SUITE] if name==PUBLIC_SUITE else xml_rows(origin.parent/'tests.xml',original.suites_for_lane(lane),failed=True)[1][name]
  # Formatting in the association view may differ; semantic fields/text must not.
  assert row.attrib==actual.attrib and [(x.tag,x.attrib,(x.text or '').strip()) for x in row]==[(x.tag,x.attrib,(x.text or '').strip()) for x in actual]
 assert result['sanitizer_environment']==({'ASAN_OPTIONS':'detect_leaks=1:halt_on_error=1','UBSAN_OPTIONS':'halt_on_error=1:print_stacktrace=1'} if lane=='san' else {})
 return result,images,inventory


def verify_result(path,lane,source,adoption_path,reference_path,namespace_preflight,live=True):
 assert live,'Recovery verification requires current bytes';path=pathlib.Path(path)
 if lane=='clang':
  frozen_recipes();origin,d,images,inventory=common_origin(lane,source,adoption_path,reference_path,namespace_preflight)
  assert path.resolve()==origin.resolve();return d,images,inventory
 return verify_candidate(s.load(path),path.parent,lane,source,adoption_path,reference_path,namespace_preflight)


def transcript(path):
 d=s.load(path);return pathlib.Path(path).parent/('associated-ctest.log' if d['status']==STATUS else 'ctest-full.log')
def execution_origin(path):
 d=s.load(path)
 return {'mode':MODE if d['status']==STATUS else 'one_actual_full_current_cohort_invocation','result':pin(path),'original_failed_result':d.get('origin_refs',{}).get('result.json'),'passed_origin_suites':d.get('passed_origin_suites',d['suite_names']),'fresh_suites':d.get('fresh_suites',[]),'full_invocation_repeated':False,'cache_hash_limitation':'original failed-run cache SHA was not finalized; current continuity attestation plus exact original built images/source/tools/dependencies and recovery before/after cache pins'}

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('lane',choices=('gcc','san'));p.add_argument('--attempt',required=True)
 for name in ('adoption','reference','namespace-preflight'):p.add_argument('--'+name,type=pathlib.Path,required=True)
 a=p.parse_args();assert re.fullmatch('[A-Za-z0-9_-]+',a.attempt);frozen_recipes();origin,d,images,inventory=common_origin(a.lane,SOURCE,a.adoption,a.reference,a.namespace_preflight)
 assert not list((W/'resume-runs').glob(a.lane+'-*')),'One authorized recovery per lane; further attempts require a new reviewed obstruction/recipe'
 out=W/'resume-runs'/(a.lane+'-'+a.attempt);out.mkdir(parents=True,exist_ok=False);before=origin_refs(origin.parent);source_before=original.source_snapshot(SOURCE,a.adoption);cache_before=pin(s.CACHES[a.lane]/'CMakeCache.txt')
 env=s.environment(a.lane)
 for key in (*original.ng.KEYS,'CMAKE_BUILD_PARALLEL_LEVEL','LD_DEBUG_OUTPUT','ASAN_OPTIONS','LSAN_OPTIONS','UBSAN_OPTIONS','JAVA_TOOL_OPTIONS','_JAVA_OPTIONS','JDK_JAVA_OPTIONS','CLASSPATH','BIGLYBTC_I2P_STREAMING_ACCEPTANCE_ROOT'):env.pop(key,None)
 if a.lane=='san':env.update(ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
 for key in ('HOME','XDG_CONFIG_HOME','XDG_CACHE_HOME','XDG_RUNTIME_DIR'):
  directory=out/key.lower();directory.mkdir(mode=0o700);env[key]=str(directory)
 authority=original.namespace_pins(a.namespace_preflight);env.update(original.ng.environment(authority,out/'public-namespace-launch.json'));runner=s.Runner(out,env)
 result={'status':'FAIL_RECOVERY_ATTEMPT','lane':a.lane,'source_commit':SOURCE,'runner_sha256':s.sha(__file__),'recovery_manifest_sha256':s.sha(RECIPES),'origin_refs':before,'original_failure_preserved':True,'obstruction':pin(OBSTRUCTION),'cache_continuity':pin(CONTINUITY),'builds_executed':False,'observation_basis':('saved actual GCC process snapshot' if a.lane=='gcc' else 'root-reported prior direct SAN observation; proc bytes not retained'),'source_receipt_sha256':s.sha(a.adoption),'reference_result_sha256':s.sha(a.reference)}
 try:
  with original.subject_phase(out):
   try:runner.run('public-tests',['nice','-n','5','ctest','--test-dir',s.CACHES[a.lane],'--parallel','1','-R','^azureus-public-bound$','--output-on-failure','--output-junit',out/'tests.xml'],timeout=300)
   finally:
    last=s.CACHES[a.lane]/'Testing/Temporary/LastTest.log'
    if last.exists():shutil.copyfile(last,out/'ctest-full.log')
  xml_rows(out/'tests.xml',[PUBLIC_SUITE]);assert not re.search(r'^(?:SKIP\s*:|FAIL!)',(out/'ctest-full.log').read_text(errors='replace'),re.M)
  receipt=original.ng.launch_receipt(out/'public-namespace-launch.json',authority,inventory[PUBLIC_SUITE]['command'][2:],original.ng.environment(authority,out/'public-namespace-launch.json'),original.PUBLIC_LAUNCHER)
  common_origin(a.lane,SOURCE,a.adoption,a.reference,a.namespace_preflight);assert before==origin_refs(origin.parent)
  association=associated_views(out,origin.parent)
  result.update(status=STATUS,subject_phase_sha256=s.sha(out/'subject-phase.json'),qualification_mode=MODE,cache=d['cache'],source_before=source_before,source_after=original.source_snapshot(SOURCE,a.adoption),images_before=images,images_after=s.images({k:pathlib.Path(v['path']) for k,v in images.items()}),current_cache_before=cache_before,current_cache_after=pin(s.CACHES[a.lane]/'CMakeCache.txt'),passed_origin_suites=sorted(set(original.suites_for_lane(a.lane))-{PUBLIC_SUITE}),fresh_suites=[PUBLIC_SUITE],suite_names=list(original.suites_for_lane(a.lane)),actual_suites=len(original.suites_for_lane(a.lane)),full_invocation_repeated=False,historical_different_source_passes_pooled=False,peer_evidence=original.peer_evidence(origin.parent),namespace_admin={**authority,'launch_environment':original.ng.environment(authority,out/'public-namespace-launch.json'),'launch_receipt':receipt},sanitizer_environment={k:env[k] for k in ('ASAN_OPTIONS','UBSAN_OPTIONS') if k in env},association=association,fresh_junit=pin(out/'tests.xml'),fresh_transcript=pin(out/'ctest-full.log'),sanitizer_origin_evidence='Original pinned runner sets these options; failed raw receipt did not finalize its sanitizer_environment field.')
  result['commands_sha256']=s.sha(out/'commands.json');verify_candidate(result,out,a.lane,SOURCE,a.adoption,a.reference,a.namespace_preflight)
 except BaseException as exc:result['status']='FAIL_RECOVERY_ATTEMPT';result['failure']=repr(exc)
 finally:result['commands_sha256']=s.sha(out/'commands.json') if (out/'commands.json').exists() else None;s.write(out/'result.json',result)
 print(result['status'],out/'result.json',flush=True);return 0 if result['status']==STATUS else 1
if __name__=='__main__':raise SystemExit(main())
