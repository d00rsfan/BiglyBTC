#!/usr/bin/env python3
"""Actual Original setSkipped/setStorageTypes and CacheFile conversions.

The supplied manager is destroyed and not constructed: DND support and later
existence/allocation are separate owner boundaries. No Core/client is started.
Requires independently attested cb8c6cb9bb97916cbc7e80338a017200ffc3e71f classes.
"""
from pathlib import Path
import argparse, hashlib, json, random, re, subprocess

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--original-classes',type=Path,required=True)
p.add_argument('--java-home',type=Path,required=True)
p.add_argument('--library',type=Path,action='append',default=[])
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();output=a.output.resolve();output.mkdir(parents=True,exist_ok=True)
classes=output/'classes';classes.mkdir(exist_ok=True);profile=output/'profile';profile.mkdir(exist_ok=True)
original=a.original_classes.resolve(strict=True);directory=Path(__file__).parent
sources=[directory/name for name in ['UnskipStorageOracle.java','CompactOwnerOracle.java','FilePriorityLoadOracle.java']]
cp=':'.join(map(str,[classes,original,*a.library]));rng=random.Random(20261119);rows=[]
for count in range(1,7):
    kinds=[[x]*count for x in range(1,5)]+[[[2,4,1,3][i%4] for i in range(count)],[[4,2,3,1][i%4] for i in range(count)]]
    masks=sorted({0,(1<<count)-1,1,1<<(count-1),sum(1<<i for i in range(0,count,2))})
    for types in kinds:
        for mask in masks:
            for skipped in [0,1]:
                for failure in [0,1,2]:
                    for initial in [-1,0]:
                        rows.append('\t'.join(map(str,[len(rows),','.join(map(str,types)),mask,skipped,failure,initial])))
inputs=output/'cases.tsv';inputs.write_text('\n'.join(rows)+'\n')
subprocess.run([str(a.java_home/'bin/javac'),'-cp',cp,'-d',str(classes),*map(str,sources)],check=True)
raw=output/'stdout.log';errors=output/'stderr.log'
with inputs.open('rb') as i,raw.open('wb') as o,errors.open('wb') as e:
    subprocess.run([str(a.java_home/'bin/java'),'-Xmx512m','-Dazureus.config.path='+str(profile),'-Dazureus.install.path='+str(output),'-Doracle.expected.classes='+str(original),'-cp',cp,'UnskipStorageOracle',str(output/'owned-files')],stdin=i,stdout=o,stderr=e,check=True,timeout=240)
actual=[line for line in raw.read_text().splitlines() if re.match(r'^\d+\t',line)]
assert len(actual)==len(rows) and all(line.split('\t')[0]==str(i) for i,line in enumerate(actual))
result=output/'original.tsv';result.write_text('\n'.join(actual)+'\n')
match=re.search(r'PASS_ORIGINAL_UNSKIP_STORAGE (\d+) rows; (\d+) actual constructors; (\d+) actual conversion requests; (\d+) cache calls',errors.read_text())
assert match and int(match[1])==len(rows)
receipt=dict(baseline='cb8c6cb9bb97916cbc7e80338a017200ffc3e71f',rows=len(rows),actual_constructors=int(match[2]),conversion_requests=int(match[3]),cache_calls=int(match[4]),failure_rows=sum('\t' in row and ';failed:' in row for row in actual),inputs_sha256=hashlib.sha256(inputs.read_bytes()).hexdigest(),output_sha256=hashlib.sha256(result.read_bytes()).hexdigest(),helpers_sha256={source.name:hashlib.sha256(source.read_bytes()).hexdigest() for source in sources})
for name in ['com/biglybt/core/disk/impl/DiskManagerFileInfoImpl.class','com/biglybt/core/disk/impl/DiskManagerFileInfoSetImpl.class','com/biglybt/core/diskmanager/cache/impl/CacheFileManagerImpl.class']:
    receipt[name]=hashlib.sha256((original/name).read_bytes()).hexdigest()
(output/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
print('PASS_ORIGINAL_UNSKIP_STORAGE_REFERENCE',len(rows),receipt['conversion_requests'],receipt['failure_rows'])
