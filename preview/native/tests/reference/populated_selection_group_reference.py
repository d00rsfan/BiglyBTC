#!/usr/bin/env python3
"""Fresh actual Original populated FileInfoSet vs emitted Native group values.

Value copies only;explicit manager destroyed/not-constructed seams suppress
DND/later allocation. Actual cache conversions and Original readers run.
"""
from pathlib import Path
import argparse,hashlib,json,os,re,shutil,subprocess,tempfile
p=argparse.ArgumentParser(description=__doc__)
for key in ['driver','original-classes','java-home','output']:p.add_argument('--'+key,type=Path,required=True)
p.add_argument('--library',type=Path,action='append',default=[])
a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);work=Path(tempfile.mkdtemp(prefix='attempt-',dir=out));classes=work/'classes';classes.mkdir();original=a.original_classes.resolve(strict=True)
directory=Path(__file__).parent;sources=[directory/name for name in ['PopulatedSelectionGroupOracle.java','ConvertedStorageReadOracle.java','CompactOwnerOracle.java','FilePriorityLoadOracle.java']]
cp=':'.join(map(str,[classes,original,*a.library]));subprocess.run([str(a.java_home/'bin/javac'),'-cp',cp,'-d',str(classes),*map(str,sources)],check=True)
native=work/'native';log=work/'native.log'
with log.open('w') as stream:r=subprocess.run([str(a.driver)],env=dict(os.environ,BIGLYBTC_SELECTION_GROUP_EXPORT=str(native)),stdout=stream,stderr=subprocess.STDOUT,timeout=180)
totals=re.findall(r'PASS_ORIGINAL_SELECTION_GROUP (\d+) families; (\d+) assertions',log.read_text());assert r.returncode==0 and totals and int(totals[-1][0])>=49,'Native group failed;see '+str(log)
def java(helper,args,label):
 command=[str(a.java_home/'bin/java'),'-Xmx256m','-Dazureus.config.path='+str(work/(label+'-config')),'-Dazureus.install.path='+str(work),'-Doracle.expected.classes='+str(original),'-cp',cp,helper,*map(str,args)]
 r=subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=90);(work/(label+'.stdout')).write_bytes(r.stdout);(work/(label+'.stderr')).write_bytes(r.stderr)
 assert r.returncode==0,'Actual Original failed;see '+str(work/(label+'.stderr'))
 return r.stdout.decode()
actual=work/'actual-original';shutil.copytree(native/'before',actual)
assert 'PASS_ORIGINAL_POPULATED_SELECTION_GROUP calls=1:1,0:3 types=RLL skipped=000' in java('PopulatedSelectionGroupOracle',[actual],'actual-group')
def values(folder):return {str(x.relative_to(folder)):hashlib.sha256(x.read_bytes()).hexdigest() for x in folder.rglob('*') if x.is_file()}
native_physical={k:v for k,v in values(native/'after').items() if not k.endswith('state.dat')}
actual_physical={k:v for k,v in values(actual).items() if not k.endswith('state.dat')}
retained='control/fmfile1.dat.bak'
assert retained in actual_physical and retained not in native_physical
assert actual_physical[retained]==values(native/'before')[retained],'Unexpected Original retained backup'
comparable_actual={k:v for k,v in actual_physical.items() if k!=retained}
assert comparable_actual==native_physical,'Actual Original populated group data/maps differ'
after=native/'after';before=values(after)
expected={'compact':hashlib.sha256((after/'data/1.bin').read_bytes()).hexdigest(),'reordered':hashlib.sha256(b'A'*16384+b'B'*16384+b'C'*16384+b'E'*4464).hexdigest()}
for variant,kind,index in [('compact',1,1),('reordered',3,0)]:
 assert 'PASS_ORIGINAL_CONVERTED_READ' in java('ConvertedStorageReadOracle',[after/'state.dat',after/'data',after/'control',kind,expected[variant],index],'read-'+variant)
assert values(after)==before,'Actual readback changed Native values'
receipt=dict(original_baseline='cb8c6cb9bb97916cbc7e80338a017200ffc3e71f',actual_populated_file_set_calls=['C index1 to L','X index0 to R'],actual_original_readbacks=2,native_families=int(totals[-1][0]),native_assertions=int(totals[-1][1]),native_physical_values=native_physical,actual_original_physical_values=actual_physical,original_retained_stale_compact_backup=retained,native_retires_both_compact_candidates=True,same_inode_whole_client_handoff=False,whole_original_core_started=False,dnd_and_later_allocation_suppressed=True,attempt=work.name,helper_sha256={x.name:hashlib.sha256(x.read_bytes()).hexdigest() for x in sources},driver_sha256=hashlib.sha256(a.driver.read_bytes()).hexdigest())
for name in ['com/biglybt/core/disk/impl/DiskManagerFileInfoImpl.class','com/biglybt/core/disk/impl/DiskManagerFileInfoSetImpl.class','com/biglybt/core/diskmanager/file/impl/FMFileAccessController.class']:receipt[name]=hashlib.sha256((original/name).read_bytes()).hexdigest()
(out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n');print('PASS_POPULATED_SELECTION_GROUP_REFERENCE 2 actual ordered cache calls;2 Original readbacks;',receipt['native_families'],'Native families')
