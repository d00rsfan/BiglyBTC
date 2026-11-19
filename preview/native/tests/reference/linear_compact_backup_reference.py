#!/usr/bin/env python3
"""Actual Original C->L,new L writes,cold L reader and Native owned value read.

Generated value copies only;not a same-inode whole-client handoff. No Original
Core or Native torrent session is activated by this storage driver.
"""
from pathlib import Path
import argparse,hashlib,json,os,re,subprocess,tempfile
p=argparse.ArgumentParser(description=__doc__)
for key in ['driver','original-classes','java-home','output']:p.add_argument('--'+key,type=Path,required=True)
p.add_argument('--library',type=Path,action='append',default=[])
a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);work=Path(tempfile.mkdtemp(prefix='attempt-',dir=out));classes=work/'classes';classes.mkdir();original=a.original_classes.resolve(strict=True);java=a.java_home/'bin'
directory=Path(__file__).parent;sources=[directory/name for name in ['LinearCompactBackupOracle.java','CompactOwnerOracle.java','FilePriorityLoadOracle.java','ConvertedStorageReadOracle.java']]
cp=':'.join(map(str,[classes,original,*a.library]));exits={}
with (work/'javac.log').open('w') as log:result=subprocess.run([str(java/'javac'),'-cp',cp,'-d',str(classes),*map(str,sources)],stdout=log,stderr=subprocess.STDOUT)
assert result.returncode==0,'Reference compile failed;see '+str(work/'javac.log')
root=work/'source';root.mkdir()
def run_java(helper,args,label):
 argv=[str(java/'java'),'-Xmx256m','-Dazureus.config.path='+str(work/(label+'-config')),'-Dazureus.install.path='+str(work),'-Doracle.expected.classes='+str(original),'-cp',cp,helper,*map(str,args)]
 with (work/(label+'.stdout')).open('w') as stdout,(work/(label+'.stderr')).open('w') as stderr:result=subprocess.run(argv,stdout=stdout,stderr=stderr,timeout=90)
 exits[label]=dict(exit=result.returncode,argv=argv);(work/'exits.json').write_text(json.dumps(exits,indent=2)+'\n');assert result.returncode==0,'Actual Original failed;see '+str(work/(label+'.stderr'))
 return (work/(label+'.stdout')).read_text()
assert 'PASS_ORIGINAL_LINEAR_WITH_COMPACT_BACKUP_AFTER_WRITE' in run_java('LinearCompactBackupOracle',[root],'produce')
expected=root/'original-linear.bin';expected_hash=hashlib.sha256(expected.read_bytes()).hexdigest()
def values():return {str(x.relative_to(root)):hashlib.sha256(x.read_bytes()).hexdigest() for x in root.rglob('*') if x.is_file()}
before=values();assert 'PASS_ORIGINAL_CONVERTED_READ kind=1' in run_java('ConvertedStorageReadOracle',[root/'state.dat',root/'data',root/'control',1,expected_hash],'cold-read');assert before==values(),'Actual cold read modified Source values'
with (work/'native.log').open('w') as log:result=subprocess.run([str(a.driver)],env=dict(os.environ,BIGLYBTC_LINEAR_BACKUP_REFERENCE=str(root)),stdout=log,stderr=subprocess.STDOUT,timeout=180)
text=(work/'native.log').read_text();totals=re.findall(r'PASS (\d+) managed-storage families; (\d+) assertions',text);assert result.returncode==0 and totals and int(totals[-1][0])>=75,'Native Source import failed;see '+str(work/'native.log')
assert 'case source-linear-with-stale-compact-backup-preserves-later-linear-writes' in text
receipt=dict(original_baseline='cb8c6cb9bb97916cbc7e80338a017200ffc3e71f',actual_c_to_l_then_l_write=True,fresh_jvm_l_read_with_only_c_backup=True,source_values_unchanged_by_cold_read=True,native_owned_io_and_checkpoint_preserve_later_linear_writes=True,native_families=int(totals[-1][0]),native_assertions=int(totals[-1][1]),same_inode_whole_client_handoff=False,whole_original_core_started=False,native_session_activated=False,attempt=work.name,expected_sha256=expected_hash,files=values(),helpers_sha256={s.name:hashlib.sha256(s.read_bytes()).hexdigest() for s in sources},driver_sha256=hashlib.sha256(a.driver.read_bytes()).hexdigest())
for name in ['com/biglybt/core/disk/impl/DiskManagerFileInfoImpl.class','com/biglybt/core/diskmanager/file/impl/FMFileAccessController.class','com/biglybt/core/diskmanager/file/impl/FMFileAccessCompact.class']:receipt[name]=hashlib.sha256((original/name).read_bytes()).hexdigest()
(out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n');print('PASS_ORIGINAL_LINEAR_COMPACT_BACKUP_REFERENCE actual C-to-L and L-write;cold Original L-read;Native owned IO/checkpoint;',receipt['native_families'],'families')
