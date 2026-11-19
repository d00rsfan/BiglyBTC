#!/usr/bin/env python3
"""Compare actual stopped Skeleton and active constructor/load with Native."""
from pathlib import Path
import argparse,hashlib,json,os,re,subprocess
p=argparse.ArgumentParser(description=__doc__)
for name in ['driver','original-classes','java-home','output']:p.add_argument('--'+name,type=Path,required=True)
p.add_argument('--library',type=Path,action='append',default=[])
a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);classes=out/'classes';classes.mkdir(exist_ok=True)
original=a.original_classes.resolve(strict=True);directory=Path(__file__).parent
sources=[directory/name for name in ['InitialFileSkeletonOracle.java','CompactOwnerOracle.java','FilePriorityLoadOracle.java']]
cp=':'.join(map(str,[classes,original,*a.library]))
subprocess.run([str(a.java_home/'bin/javac'),'-cp',cp,'-d',str(classes),*map(str,sources)],check=True)
rows=[]
for kind,code in [('L',1),('C',2),('R',3),('X',4)]:
 for mode,priority in enumerate(['absent','-1','0','1','-2']):rows.append(f'{kind}-{mode}\t4096\t{code}\t{priority}\t0\t0\n')
inputs=''.join(rows);(out/'cases.tsv').write_text(inputs)
results={}
for name,helper in [('active','CompactOwnerOracle'),('stopped','InitialFileSkeletonOracle')]:
 work=out/name;work.mkdir(exist_ok=True)
 command=[str(a.java_home/'bin/java'),'-Xmx256m','-Dazureus.config.path='+str(work/'config'),'-Dazureus.install.path='+str(work),'-Doracle.expected.classes='+str(original),'-cp',cp,helper,str(work/'fixtures')]
 r=subprocess.run(command,input=inputs,text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=90)
 (out/(name+'.tsv')).write_text(r.stdout);(out/(name+'.log')).write_text(r.stderr)
 assert r.returncode==0,'Actual Original '+name+' failed;see '+str(out)
 results[name]={f[0]:f[1] for line in r.stdout.splitlines() if (f:=line.split('\t')) and len(f)>=2}
 assert len(results[name])==20
log=out/'native.log'
with log.open('w') as stream:r=subprocess.run([str(a.driver)],stdout=stream,stderr=subprocess.STDOUT,timeout=120)
s=log.read_text();assert r.returncode==0 and 'PASS 68 managed-storage families; 6919 assertions' in s
observed=re.findall(r'^OBS_INITIAL_POLICY\t([LCRX])\t([0-4])\t([01])\t([01])\t(-?\d+)$',s,re.M);assert len(observed)==20
for kind,mode,stopped,active,priority in observed:
 key=kind+'-'+mode;assert results['stopped'][key]==stopped+':'+priority,key
 assert results['active'][key]==active+':'+priority,key
receipt=dict(original_baseline='cb8c6cb9bb97916cbc7e80338a017200ffc3e71f',rows=20,actual_stopped_factory_rows=20,actual_active_constructors=20,explicit_active_requested_type_getter=True,whole_original_core_started=False,helpers_sha256={x.name:hashlib.sha256(x.read_bytes()).hexdigest() for x in sources},driver_sha256=hashlib.sha256(a.driver.read_bytes()).hexdigest(),artifacts_sha256={x.name:hashlib.sha256(x.read_bytes()).hexdigest() for x in [log,out/'cases.tsv',out/'active.tsv',out/'stopped.tsv']})
for name in ['com/biglybt/core/disk/impl/DiskManagerUtil.class','com/biglybt/core/disk/impl/DiskManagerFileInfoImpl.class']:receipt[name]=hashlib.sha256((original/name).read_bytes()).hexdigest()
(out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
print('PASS_INITIAL_FILE_POLICY_REFERENCE 20 actual stopped factories;20 active constructors;20 Native owner/details rows')
