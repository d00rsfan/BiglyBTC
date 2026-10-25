from pathlib import Path
import gzip,hashlib,json,shutil,xml.etree.ElementTree as E
w=Path(__file__).resolve().parent;r=Path('/home/yurii/Java/BiglyBT');d=r/'migration/evidence/20261025-storage-resume';n=r.parent/'BiglyBTC';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();source=(w/'final-source-commit.txt').read_text().strip();final=json.loads((w/'gcc-r9-source.json').read_text());assert all(sha(n/k)==v for k,v in final.items());d.mkdir(exist_ok=True);(d/'.gitattributes').write_text('* -text\n');lanes={}
for case,count in [('gcc-functional-r9',9),('gcc-regression-r7-tests',14),('clang-r9-tests',23),('asan-r9-tests',23)]:
 receipt=json.loads((w/(case+'-receipt.json')).read_text());assert receipt['exit']==0;xml=E.parse(w/(case+'.xml'));tests=list(xml.iter('testcase'));assert len(tests)==count and not list(xml.iter('failure'))and not list(xml.iter('error'))and not list(xml.iter('skipped'))
 snapshot=json.loads((w/(case+'-source.json')).read_text());delta=sorted(k for k in set(final)|set(snapshot)if final.get(k)!=snapshot.get(k));assert not delta if case!='gcc-regression-r7-tests' else len(delta)<=6
 lanes[case]={'suites':count,'final_source_delta':delta,'receipt_sha256':sha(w/(case+'-receipt.json'))}
 for suffix in ['-receipt.json','-source.json','.xml']:shutil.copy2(w/(case+suffix),d/(case+suffix))
for p in w.glob('*.log'):(d/(p.name+'.gz')).write_bytes(gzip.compress(p.read_bytes(),mtime=0))
for p in list(w.glob('*.py'))+list(w.glob('*.json')):shutil.copy2(p,d/p.name)
shutil.copy2(w/'QUESTIONS.md',d/'QUESTIONS.md')
for name in ['oracle','oracle-next','oracle-codec-r9','oracle-managed-r8','defaults-all-r8']:
 shutil.copytree(w/name,d/name,dirs_exist_ok=True,ignore=shutil.ignore_patterns('__pycache__','*.class','*.so','*.a','*.keystore'))
for name in ['large-piece','negative-links','save-defaults','reordered-completion','pending-completion-fileops']:
 shutil.copytree(w/'remainder'/name,d/'capsules'/name,dirs_exist_ok=True,ignore=shutil.ignore_patterns('__pycache__'))
publication=n/'build/storage-20261025/portable/core-r9';private=json.loads((publication/'verification/result.json').read_text());assert private['native_source']==source and private['suite_count']==12 and private['smoke_qt_rows']==13
shutil.copytree(w/'portable',d/'portable-recipes',dirs_exist_ok=True);shutil.copytree(publication/'verification',d/'portable',dirs_exist_ok=True)
for p in Path(private['sample_root']).glob('*.png'):(d/'captures').mkdir(exist_ok=True);shutil.copy2(p,d/'captures'/p.name)
record={'status':'LOCAL_QUALIFIED_HOSTED_AND_PROMOTION_PENDING','original_baseline':'cb8c6cb9bb97916cbc7e80338a017200ffc3e71f','native_parent':'b44ec22ebb3dbb6cee3ade23c32a7ad26a373926','native_source':source,'compiler_lanes':lanes,'actual_codec':json.loads((w/'oracle-codec-r9/result.json').read_text()),'actual_managed':json.loads((w/'oracle-managed-r8/result.json').read_text()),'actual_defaults':json.loads((w/'defaults-all-r8/result.json').read_text()),'portable':private,'open_human_questions':[],'original_profile_or_process_access':False,'original_core_recompiled':False,'system_installation':False,'sanitizer_scope':'Native ASan/UBSan/leaks; existing managed_payload_counters.cpp private-provider vptr-only exception unchanged; shipped provider itself is not fully sanitized.','whole_CF01_complete':False}
(d/'verification.json').write_text(json.dumps(record,indent=2)+'\n');print('Retained local evidence for',source,'with explicit pending hosted/promotion gates')
