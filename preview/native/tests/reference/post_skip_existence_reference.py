#!/usr/bin/env python3
"""Current constructed existence effects;no Original Core/network startup.

Only DiskManagerImpl.<clinit> is derived;all other methods retain exact bytes.
The requestAllocation dependency records requests;it does not run the allocator.
"""
from pathlib import Path
import hashlib,itertools,json,random,runpy,subprocess,tempfile
import argparse
p=argparse.ArgumentParser(description="Actual Original post-skip existence matrix;isolated scheduler bootstrap only")
for key in ['driver','original-classes','java-home','output']:p.add_argument('--'+key,type=Path,required=True)
p.add_argument('--library',type=Path,action='append',default=[])
a=p.parse_args();D=a.output.resolve();D.mkdir(parents=True,exist_ok=True);directory=Path(__file__).resolve().parent;original=a.original_classes.resolve(strict=True);J=a.java_home/'bin';W=Path(tempfile.mkdtemp(prefix='matrix-',dir=D));C=W/'classes';C.mkdir()
expected={'com/biglybt/core/disk/impl/DiskManagerUtil.class': '7ca472378479aedfaaba98539821218edcd5526a18c45ea3426ad74f4652e7fa', 'com/biglybt/core/disk/impl/resume/RDResumeHandler.class': '1205d10be1a8764f1cbb44ad25cd8c17357c87877d43253ad1faf665872abfb7', 'com/biglybt/core/disk/impl/DiskManagerFileInfoImpl.class': '3034abc2db2a20a640ea20a5d5b39ffbeb037794762db0e4647aba4b933a5f6e', 'com/biglybt/core/disk/impl/DiskManagerFileInfoSetImpl.class': '9e487c9917ea057a9d9f63796502c0f19d50dfceaa5d78b638d56c7bb69e2a99'}
expected['com/biglybt/core/disk/impl/DiskManagerImpl.class']='719f27ad43b88cb10bd4f9b8b34c6e9a3aea11c9f4a6fabc7fd3cb192066fb79'
for path,digest in expected.items():assert hashlib.sha256((original/path).read_bytes()).hexdigest()==digest,'Wrong pinned Original class: '+path
seam=runpy.run_path(str(directory/'post_skip_bootstrap.py'))['prepare'](original,C)
cp=':'.join(map(str,[C,original,*a.library]));sources=[directory/s for s in ['PostSkipExistenceOracle.java','CompactOwnerOracle.java','FilePriorityLoadOracle.java']]
with (W/'javac.log').open('w') as log:result=subprocess.run([str(J/'javac'),'-cp',cp,'-d',str(C),*map(str,sources)],stdout=log,stderr=subprocess.STDOUT)
assert result.returncode==0,str(W/'javac.log')
rows=[]
def add(lengths,kinds,skip,change,exists,flag,constructed,resume):
 constructed=int(constructed);rows.append('\t'.join(map(str,[len(rows),','.join(map(str,lengths)),','.join(map(str,kinds)),skip,change,exists,flag,constructed,resume])))
for length in [1,4096,16384,16385]:
 for kind,skip,change,exists,flag,constructed,resume in itertools.product(range(1,5),range(2),range(2),range(2),range(2),range(2),range(4)):
  add([length],[kind],skip,change,exists,flag,constructed,resume)
for kind,skip,change,exists,flag,constructed in itertools.product(range(1,5),range(2),range(2),range(2),range(2),range(2)):
 add([0],[kind],skip,change,exists,flag,constructed,0)
for lengths in [[8192,8192],[16384,1]]:
 for kinds,skip,change,exists in itertools.product(itertools.product(range(1,5),repeat=2),range(4),range(4),[0,3]):
  add(lengths,kinds,skip,change,exists,(skip+change)%2,1,(skip*3+change+sum(kinds))%5)
rng=random.Random(20261119)
for lengths in [[0,1],[1,0],[8192,8192],[16384,0,1],[8192,0,8192],[0,0,1],[16385,0,16383],[0,16384,0,16384]]:
 size=len(lengths)
 for case in range(128):add(lengths,[rng.randrange(1,5) for _ in lengths],rng.randrange(1<<size),rng.randrange(1<<size),rng.randrange(1<<size),rng.randrange(2),case%7!=0,case%5)
text='\n'.join(rows)+'\n';(W/'input.tsv').write_text(text)
argv=[str(J/'java'),'-Xmx256m','-Dazureus.config.path='+str(W/'config'),'-Dazureus.install.path='+str(W),'-Doracle.expected.classes='+str(original),'-Doracle.expected.disk.classes='+str(C),'-cp',cp,'PostSkipExistenceOracle',str(W/'fixtures')]
with (W/'stdout').open('w') as stdout,(W/'stderr').open('w') as stderr:result=subprocess.run(argv,input=text,text=True,stdout=stdout,stderr=stderr,timeout=600)
(W/'exit.json').write_text(json.dumps(dict(exit=result.returncode,argv=argv),indent=2)+'\n');assert result.returncode==0,str(W/'stderr')
observations=[line for line in (W/'stdout').read_text().splitlines() if line.split('\t',1)[0].isdigit()];assert len(observations)==len(rows),(len(observations),len(rows))
for index,line in enumerate(observations):assert int(line.split('\t')[0])==index
(W/'observations.tsv').write_text('\n'.join(observations)+'\n')
receipt=dict(original_baseline='cb8c6cb9bb97916cbc7e80338a017200ffc3e71f',rows=len(rows),actual_constructors=sum(len(row.split('\t')[1].split(',')) for row in rows),actual_root_and_predicate_unmodified=True,disk_manager_initializer_only_seam=True,all_other_disk_methods_identical=all(not x['changed'] or name=='<clinit>()V' for name,x in seam['methods'].items()),explicit_kind_getter_domain=True,original_core_created=False,attempt=W.name,input_sha256=hashlib.sha256(text.encode()).hexdigest(),observations_sha256=hashlib.sha256((W/'observations.tsv').read_bytes()).hexdigest(),helpers_sha256={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sources},bootstrap_seam=seam)
for name in ['com/biglybt/core/disk/impl/DiskManagerUtil.class','com/biglybt/core/disk/impl/resume/RDResumeHandler.class','com/biglybt/core/disk/impl/DiskManagerFileInfoImpl.class','com/biglybt/core/disk/impl/DiskManagerFileInfoSetImpl.class']:receipt[name]=hashlib.sha256((original/name).read_bytes()).hexdigest()
(D/'original-matrix-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n');native=subprocess.run([str(a.driver),'--oracle'],input=text,text=True,capture_output=True,timeout=30);(W/'native.stdout').write_text(native.stdout);(W/'native.stderr').write_text(native.stderr);assert native.returncode==0,'Native matrix failed'
assert native.stdout==(W/'observations.tsv').read_text(),'Source/Native post-skip matrix differs'
receipt.update(native_all_fields_equal=True,driver_sha256=hashlib.sha256(a.driver.read_bytes()).hexdigest())
(D/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n');print('PASS_ORIGINAL_POST_SKIP_REFERENCE',len(rows),'rows;',receipt['actual_constructors'],'actual constructors;all four Native masks equal')
