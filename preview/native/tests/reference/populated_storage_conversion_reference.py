#!/usr/bin/env python3
"""Compare populated actual Original transitions with shared Native storage.

Reads emitted generated Native fixture bytes. This is format/logical readback,
not a same-inode whole-client handoff. No Original Core is created or started.
"""
from pathlib import Path
import argparse,hashlib,json,os,re,subprocess,tempfile
p=argparse.ArgumentParser(description=__doc__)
for key in ['driver','original-classes','java-home','output']:p.add_argument('--'+key,type=Path,required=True)
p.add_argument('--library',type=Path,action='append',default=[])
a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);work=Path(tempfile.mkdtemp(prefix='attempt-',dir=out));classes=work/'classes';classes.mkdir();original=a.original_classes.resolve(strict=True)
directory=Path(__file__).parent;sources=[directory/name for name in ['PopulatedCompactTransitionOracle.java','PopulatedReorderedTransitionOracle.java','ConvertedStorageReadOracle.java','CompactOwnerOracle.java','FilePriorityLoadOracle.java']]
cp=':'.join(map(str,[classes,original,*a.library]));subprocess.run([str(a.java_home/'bin/javac'),'-cp',cp,'-d',str(classes),*map(str,sources)],check=True)
def java(helper,args,label):
 config=work/(label+'-config');command=[str(a.java_home/'bin/java'),'-Xmx256m','-Dazureus.config.path='+str(config),'-Dazureus.install.path='+str(work),'-Doracle.expected.classes='+str(original),'-cp',cp,helper,*map(str,args)]
 r=subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=90)
 (work/(label+'.stdout')).write_bytes(r.stdout);(work/(label+'.stderr')).write_bytes(r.stderr)
 assert r.returncode==0,'Actual Original failed;see '+str(work/(label+'.stderr'))
 return r.stdout.decode()
compact=work/'original-compact';reordered=work/'original-reordered'
assert 'PASS_ORIGINAL_POPULATED_COMPACT_TO_LINEAR' in java('PopulatedCompactTransitionOracle',[compact],'compact')
assert 'PASS_ORIGINAL_POPULATED_X_TO_R' in java('PopulatedReorderedTransitionOracle',[reordered],'reordered')
env=dict(os.environ,BIGLYBTC_PACKED_LINEAR_REFERENCE=str(compact/'original-linear.bin'),BIGLYBTC_CONVERSION_NATIVE_EXPORT=str(work/'native-export'))
log=work/'native.log'
with log.open('w') as stream:r=subprocess.run([str(a.driver)],env=env,stdout=stream,stderr=subprocess.STDOUT,timeout=180)
text=log.read_text();totals=re.findall(r'PASS (\d+) managed-storage families; (\d+) assertions',text);assert r.returncode==0 and totals and int(totals[-1][0])>=71,'Actual Native failed;see '+str(log)
expected={'compact':hashlib.sha256((compact/'original-linear.bin').read_bytes()).hexdigest(),'reordered':hashlib.sha256(b'A'*16384+b'B'*16384+b'C'*16384+b'E'*4464).hexdigest()}
for variant,kind in [('compact',1),('reordered',3)]:
 target=work/'native-export'/variant;files=[x for x in target.rglob('*') if x.is_file()];before={str(x.relative_to(target)):hashlib.sha256(x.read_bytes()).hexdigest() for x in files}
 assert 'PASS_ORIGINAL_CONVERTED_READ' in java('ConvertedStorageReadOracle',[target/'state.dat',target/'data',target/'control',kind,expected[variant]],'read-'+variant)
 after={str(x.relative_to(target)):hashlib.sha256(x.read_bytes()).hexdigest() for x in target.rglob('*') if x.is_file()};assert before==after,'Readback changed emitted Native fixture'
receipt=dict(original_baseline='cb8c6cb9bb97916cbc7e80338a017200ffc3e71f',actual_populated_original_transitions=2,actual_original_readbacks=2,native_families=int(totals[-1][0]),native_assertions=int(totals[-1][1]),emitted_native_fixture_readback=True,same_inode_whole_client_handoff=False,whole_original_core_started=False,attempt=work.name,helper_sha256={x.name:hashlib.sha256(x.read_bytes()).hexdigest() for x in sources},driver_sha256=hashlib.sha256(a.driver.read_bytes()).hexdigest(),native_log_sha256=hashlib.sha256(log.read_bytes()).hexdigest())
for name in ['com/biglybt/core/disk/impl/DiskManagerFileInfoImpl.class','com/biglybt/core/diskmanager/file/impl/FMFileAccessController.class','com/biglybt/core/diskmanager/file/impl/FMFileAccessPieceReorderer.class']:receipt[name]=hashlib.sha256((original/name).read_bytes()).hexdigest()
(out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n');print('PASS_POPULATED_STORAGE_REFERENCE 2 actual transitions;2 Original readbacks;',receipt['native_families'],'Native families')
