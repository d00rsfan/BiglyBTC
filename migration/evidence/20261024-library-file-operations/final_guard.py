from pathlib import Path
import hashlib,json,subprocess
r=Path('/home/yurii/Java/BiglyBT');n=r.parent/'BiglyBTC';w=Path(__file__).resolve().parent;sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();start=json.loads((w/'start.json').read_text());meta=(w/'final-metadata-commit.txt').read_text().strip();source=(w/'final-source-commit.txt').read_text().strip()
assert subprocess.check_output(['git','rev-parse','HEAD'],cwd=r,text=True).strip()==start['original_head'];assert sha(r/'.git/index')==start['original_index_sha256']
assert subprocess.check_output(['git','rev-parse','HEAD'],cwd=n,text=True).strip()==meta;assert not subprocess.check_output(['git','status','--porcelain'],cwd=n,text=True).strip()
remote=subprocess.check_output(['git','ls-remote','origin','refs/heads/main'],cwd=n,text=True).split()[0];assert remote==meta
subprocess.run(['git','merge-base','--is-ancestor',source,meta],cwd=n,check=True)
for name,digest in json.loads((w/'final-source.json').read_text()).items():assert sha(n/name)==digest
manifest=json.loads((n/'dist/BUILD_MANIFEST.json').read_text());promotion=json.loads((n/'build/fileops-20261024/portable/fileops-final/promotion.json').read_text());assert sha(n/'dist/BUILD_MANIFEST.json')==promotion['manifest_sha256']
for name,digest in manifest['files_sha256'].items():assert sha(n/'dist'/name)==digest
checkpoint=json.loads((w/'original-checkpoint.json').read_text());assert checkpoint['original_head_unchanged']==start['original_head'] and checkpoint['original_index_sha256_unchanged']==start['original_index_sha256']
record={'status':'CHAPTER_CLOSED','native_source':source,'native_metadata':meta,'remote_main':remote,'source_files':len(json.loads((w/'final-source.json').read_text())),'dist_files':len(manifest['files_sha256']),'dist_manifest_sha256':promotion['manifest_sha256'],'original_checkpoint':checkpoint['commit'],'original_head':start['original_head'],'original_index_sha256':start['original_index_sha256'],'original_profile_or_process_access':False,'system_installation':False,'next_chapter_started':False}
(w/'final-guard.json').write_text(json.dumps(record,indent=2)+'\n');print(json.dumps(record))
