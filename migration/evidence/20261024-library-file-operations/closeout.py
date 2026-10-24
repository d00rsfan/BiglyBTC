"""Retain the executed CF-06 qualification after successful portable promotion."""
from pathlib import Path
import gzip,hashlib,json,re,shutil,subprocess,xml.etree.ElementTree as E
R=Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';W=Path(__file__).resolve().parent
D=R/'migration/evidence/20261024-library-file-operations';D.mkdir(parents=True,exist_ok=True)
def load(p):return json.loads(p.read_text())
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def write(p,data):p.parent.mkdir(parents=True,exist_ok=True);p.write_text(json.dumps(data,indent=2)+'\n')
def tree(src,dst,extra=()):
 shutil.copytree(src,dst,dirs_exist_ok=True,ignore=shutil.ignore_patterns('__pycache__','*.class','*.o','*.so','*.a','*.keystore',*extra))
source=(W/'final-source-commit.txt').read_text().strip();final=load(W/'final-source.json')
assert subprocess.check_output(['git','rev-parse','HEAD'],cwd=N,text=True).strip()==source
assert all(sha(N/p)==v for p,v in final.items())
lanes={};gcc=set()
for case,count in [('gcc-r1',19),('gcc-r3',9),('gcc-r4',2),('clang-r1',12),('clang-r2',2),('sanitized-r1',12),('sanitized-r2',2)]:
 receipt=load(W/(case+'-receipt.json'));assert receipt['commands'] and all(c['exit']==0 for c in receipt['commands'])
 xml=E.parse(W/(case+'.xml'));tests=list(xml.iter('testcase'));assert len(tests)==count and not list(xml.iter('failure')) and not list(xml.iter('error')) and not list(xml.iter('skipped'))
 snapshot=load(W/(case+'-source.json'))
 if case in ['gcc-r4','clang-r2','sanitized-r2']:assert snapshot==final
 if case in ['clang-r1','sanitized-r1']:assert snapshot==load(W/'pre-pause-source.json')
 if case=='gcc-r3':assert {p for p in final if final[p]!=snapshot[p]}=={'preview/native/tests/file_operations_driver.cpp','preview/native/adapters/file_operations.cpp','preview/native/tests/file_operations_tests.cpp'}
 if case.startswith('gcc'):gcc.update(t.attrib['name'] for t in tests)
 lanes[case]={'suites':count,'receipt_sha256':sha(W/(case+'-receipt.json'))}
 for suffix in ['-receipt.json','-source.json','.xml']:shutil.copy2(W/(case+suffix),D/(case+suffix))
assert len(gcc)==20
for p in W.glob('*.log'):(D/(p.name+'.gz')).write_bytes(gzip.compress(p.read_bytes(),mtime=0))
for p in W.glob('*.py'):shutil.copy2(p,D/p.name)
for p in W.glob('*.json'):shutil.copy2(p,D/p.name)
shutil.copy2(W/'QUESTIONS.md',D/'QUESTIONS.md')
for name in ['storage','storage-driver','creator','ui','reviews','corrections','java-handoff']:
 tree(W/name,D/'capsules'/name,('run-r*','handoff-r*'))
for name in ['root/integration-inputs.json','root/integrate.py','root/cmake.py']:
 p=W/name;(D/'integration').mkdir(exist_ok=True);shutil.copy2(p,D/'integration'/p.name)
original=load(W/'java-handoff/run-r4/result.json');creator=load(W/'creator/handoff-r1/result.json')
assert original['status']=='PASS_ACTUAL_NATIVE_FILE_OPERATIONS_ORIGINAL_JAVA' and len(original['cases'])==4
assert creator['status']=='PASS_ACTUAL_NATIVE_CREATION_ORIGINAL_JAVA_PARSE' and len(creator['cases'])==3
for src,name in [(W/'java-handoff/run-r4','original-fileops'),(W/'creator/handoff-r1','original-creator')]:tree(src,D/name)
for attempt in ['run-r1','run-r2','run-r3']:
 src=W/'java-handoff'/attempt;dst=D/'failed-fixtures'/attempt;dst.mkdir(parents=True,exist_ok=True)
 for p in src.iterdir():
  if p.is_file():shutil.copy2(p,dst/p.name)
publication=N/'build/fileops-20261024/portable/fileops-final'
private=load(publication/'verification/result.json');promotion=load(publication/'promotion.json')
assert private['native_source']==source==promotion['native_source']
assert private['manifest_sha256']==promotion['manifest_sha256']==sha(N/'dist/BUILD_MANIFEST.json')
assert private['suite_count']==6 and private['smoke_qt_rows']==13
assert load(W/'portable-gcc-refresh.json')['exit']==0
hosted=load(W/'final-hosted-final.json');assert len(hosted)==2 and all(r['headSha']==source and r['conclusion']=='success' for r in hosted)
counts={}
for row in hosted:
 p=W/('hosted-'+str(row['databaseId'])+'.log');found=re.findall(r'100% tests passed, 0 tests failed out of (\d+)',p.read_text());assert found
 counts[row['name']]=int(found[-1])
tree(W/'portable',D/'portable-recipes');tree(publication/'verification',D/'portable')
shutil.copy2(publication/'promotion.json',D/'promotion.json');shutil.copy2(N/'dist/BUILD_MANIFEST.json',D/'BUILD_MANIFEST.json')
for root in [W/'gcc-r3-captures',Path(private['sample_root'])]:
 for p in root.glob('*.png'):(D/'captures').mkdir(exist_ok=True);shutil.copy2(p,D/'captures'/p.name)
record={'status':'QUALIFIED_BOUNDED_LIBRARY_FILE_OPERATIONS','original_baseline':load(W/'start.json')['original_head'],'native_start':load(W/'start.json')['native_start'],'native_source':source,
 'compiler_lanes':lanes,'gcc_unique_suites':len(gcc),'gcc_scope':'19 initial and9 integration suites cover20 unique suites, followed by2 affected reruns after the actual-Java-discovered modern pause-map fix. Clang/ASan each pass12 initial suites and then2 affected suites against final sources; only the two-file narrow fix differs. All4 original handoffs use the final implementation.',
 'original_creation_cases':creator,'original_file_operations_cases':original,'hosted':hosted,'hosted_counts':counts,'portable':private,'promotion':promotion,
 'open_human_questions':[],'original_profile_or_process_access':False,'system_installation':False,
 'sanitizer_scope':'ASan/UBSan/leaks on native, with the preexisting managed_payload_counters.cpp vptr-only exception required by private LT RTTI. The shipped uninstrumented provider is not claimed fully sanitized.',
 'scope_record':'scope.md','remaining_original_families':['bulk operations','advanced/v2/hybrid/virtual-source torrent wizard and256MiB manual pieces','archive deletion controls','external/nonlinear/shared/overlapping storage and merge/overwrite moves','wider interrupted-operation repair UX','full-payload validation IO in non-Keep file operations; compositor/public-swarm/performance qualification']}
write(D/'verification.json',record);print(json.dumps({'source':source,'evidence':str(D),'hosted_counts':counts,'manifest':private['manifest_sha256']}))
