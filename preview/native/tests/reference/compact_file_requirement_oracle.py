#!/usr/bin/env python3
"""Generate/compare compact availability with original cb8c6cb9bb97916cbc7e80338a017200ffc3e71f classes.

Reference tooling only. Requires a separately attested original build and JDK17;
all Java configuration/output lives in the explicitly selected output directory.
The native application has no JVM dependency.
"""
from pathlib import Path
import argparse,hashlib,json,random,subprocess
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--original-classes',type=Path,required=True)
p.add_argument('--java-home',type=Path,required=True)
p.add_argument('--library',type=Path,action='append',default=[])
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();output=a.output.resolve();output.mkdir(parents=True,exist_ok=True)
classes=output/'classes';classes.mkdir(exist_ok=True);profile=output/'profile';profile.mkdir(exist_ok=True)
original=a.original_classes.resolve(strict=True)
source=Path(__file__).with_name('CompactAvailabilityOracle.java')
cp=':'.join(map(str,[classes,original,*a.library]))
rng=random.Random(20261118);rows=[];number=0
for count in range(1,9):
 for generation in range(8):
  lengths=[rng.choice([0,1,8191,16383,16384,16385,32768,49153]) for _ in range(count)]
  if not any(lengths):lengths[-1]=1
  masks=range(1<<count) if count<=4 else sorted({0,(1<<count)-1,*[rng.randrange(1<<count) for _ in range(14)]})
  for mask in masks:
   for target in range(count):
    for compact in [2,4]:
     for mode in [0,1,2]:
      rows.append('\t'.join(map(str,[number,','.join(map(str,lengths)),target,mask,compact,mode])));number+=1
inputs=output/'cases.tsv';inputs.write_text('\n'.join(rows)+'\n')
subprocess.run([str(a.java_home/'bin/javac'),'-cp',cp,'-d',str(classes),str(source)],check=True)
result=output/'original.tsv'
with inputs.open('rb') as i,result.open('wb') as o:
 subprocess.run([str(a.java_home/'bin/java'),'-Xmx256m','-Dazureus.config.path='+str(profile),'-Dazureus.install.path='+str(output),'-Doracle.expected.classes='+str(original),'-cp',cp,'CompactAvailabilityOracle'],stdin=i,stdout=o,check=True)
receipt={'baseline':'cb8c6cb9bb97916cbc7e80338a017200ffc3e71f','rows':number,'inputs_sha256':hashlib.sha256(inputs.read_bytes()).hexdigest(),'output_sha256':hashlib.sha256(result.read_bytes()).hexdigest(),'helper_sha256':hashlib.sha256(source.read_bytes()).hexdigest()}
for name in ['com/biglybt/core/disk/impl/resume/RDResumeHandler.class','com/biglybt/core/torrent/TOTorrentFactory.class','com/biglybt/core/torrent/impl/TOTorrentFileImpl.class']:
 receipt[name]=hashlib.sha256((original/name).read_bytes()).hexdigest()
(output/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
print('PASS_ORIGINAL_COMPACT_REFERENCE',number)
