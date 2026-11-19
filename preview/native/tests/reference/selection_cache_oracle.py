#!/usr/bin/env python3
"""Read Native explicit-selection output with actual original cache classes.

Requires an independently attested cb8c6cb9bb97916cbc7e80338a017200ffc3e71f
artifact and the native generated-profile Qt driver. Never opens a user profile.
"""
from pathlib import Path
import argparse, hashlib, json, os, re, subprocess

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--driver',type=Path,required=True)
p.add_argument('--original-classes',type=Path,required=True)
p.add_argument('--java-home',type=Path,required=True)
p.add_argument('--library',type=Path,action='append',default=[])
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();output=a.output.resolve();output.mkdir(parents=True,exist_ok=True)
classes=output/'classes';classes.mkdir(exist_ok=True);original=a.original_classes.resolve(strict=True)
directory=Path(__file__).parent;sources=[directory/name for name in ['SelectionCacheReadOracle.java','FilePriorityLoadOracle.java']]
cp=':'.join(map(str,[classes,original,*a.library]))
subprocess.run([str(a.java_home/'bin/javac'),'-cp',cp,'-d',str(classes),*map(str,sources)],check=True)
env=dict(os.environ,BIGLYBTC_SELECTION_JAVA=str(a.java_home/'bin/java'),BIGLYBTC_SELECTION_CLASSPATH=cp,BIGLYBTC_SELECTION_ORIGINAL_CLASSES=str(original),QT_QPA_PLATFORM='offscreen')
log=output/'native.log'
with log.open('w') as out:
    result=subprocess.run([str(a.driver)],env=env,stdout=out,stderr=subprocess.STDOUT,timeout=180)
text=log.read_text();totals=re.findall(r'Totals: (\d+) passed, (\d+) failed, (\d+) skipped',text)
assert result.returncode==0 and totals and all(f=='0' and s=='0' for passed,f,s in totals),'Actual native/reference suite failed;see '+str(log)
markers=re.findall(r'PASS_ORIGINAL_SELECTION_CACHE_READ kind=(\d+) bytes=(\d+)',text)
assert sorted(markers)==[('1','4096')]*4+[('3','4096')]*4
receipt=dict(original_baseline='cb8c6cb9bb97916cbc7e80338a017200ffc3e71f',actual_original_cache_reads=len(markers),plain_and_gzip=True,qt_passed=sum(int(p) for p,f,s in totals),helpers_sha256={source.name:hashlib.sha256(source.read_bytes()).hexdigest() for source in sources},driver_sha256=hashlib.sha256(a.driver.read_bytes()).hexdigest(),log_sha256=hashlib.sha256(log.read_bytes()).hexdigest(),whole_original_client_started=False)
for name in ['com/biglybt/core/disk/impl/DiskManagerFileInfoImpl.class','com/biglybt/core/diskmanager/file/impl/FMFileAccessController.class','com/biglybt/core/torrent/TOTorrentFactory.class']:
    receipt[name]=hashlib.sha256((original/name).read_bytes()).hexdigest()
(output/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
print('PASS_ORIGINAL_SELECTION_CACHE_READBACK',len(markers),'actual reads;',receipt['qt_passed'],'Qt cases')
