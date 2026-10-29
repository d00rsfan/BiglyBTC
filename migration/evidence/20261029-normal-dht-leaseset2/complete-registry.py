#!/usr/bin/env python3
"""Complete metadata-only promised-object retention followup; no build/test/promotion."""
import pathlib,json,hashlib,shutil,subprocess,os
R=pathlib.Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';H=pathlib.Path(__file__).resolve().parent;E=pathlib.Path('migration/evidence/20261029-normal-dht-leaseset2')
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
promotion=json.loads((H/'promotion.json').read_bytes());assert promotion['manifest_sha256']==sha(N/'dist/BUILD_MANIFEST.json')=='92e3b83ba74ee2ec3b1886521914370a31074533daed7f2ed11d058804f2ab68'
adoption=json.loads((H/'root-adoption.json').read_bytes());assert adoption['source_commit']=='6726a4cf7b017ac31d6aeede15f339c3ec2b71f7'
for name,row in adoption['source_paths'].items():assert sha(N/name)==row['after_sha256']
retention=json.loads((H/'upstream-object-retention.json').read_bytes());assert len(retention)==28 and all(r['exit']==0 for r in retention)
logs={}
for label,root in [('R',R),('N',N)]:
 log=H/('registry-validation-'+label+'-r2.log')
 with log.open('wb') as stream:p=subprocess.run(['python3','-B',root/'migration/tools/validate.py'],cwd=root,env=dict(os.environ,GIT_NO_LAZY_FETCH='1',GIT_OPTIONAL_LOCKS='0'),stdout=stream,stderr=subprocess.STDOUT)
 assert p.returncode==0,log;assert 'PASS registry: 20 sources, 108 components' in log.read_text();logs[label]={'path':str(log),'sha256':sha(log)}
for root in (R,N):
 out=root/E
 for source,name in [(H/'registry-validation-BiglyBT.log','registry-initial-missing-promised-object.log'),(H/'upstream-object-retention.json','upstream-object-retention.json'),(H/'finish-source-review.json','finish-source-review.json'),(H/'complete-registry.py','complete-registry.py'),*[(pathlib.Path(row['path']),'registry-validation-'+label+'-r2.log') for label,row in logs.items()]]:shutil.copyfile(source,out/name)
 d=json.loads((out/'verification.json').read_bytes());d['registry_validation']=logs;d['registry_retention_correction']='Initial post-promotion validator lacked promised Gitobjects. Exact28 baseline paths retained, reviewed local LS2 bytes matched; both offline validators nowPASS. No source/build/test/Java/promotion replay.';d['unchanged_registry_counts']={'sources':20,'components':108,'external_dependencies':22}
 (out/'verification.json').write_text(json.dumps(d,indent=2)+'\n')
 with (out/'REVIEW.md').open('a') as stream:stream.write('\nInitial post-promotion registry validation failed because a promised upstream Gitblob was absent locally. Exact28 baseline paths were retained and reviewed LS2 source bytes matched. Both validators then passed offline. The first log and retention receipt are preserved; no source/build/test/Java/promotion was repeated.\n')
print('BOTH_REGISTRIES_PASS20/108/22; no runtime replay')
