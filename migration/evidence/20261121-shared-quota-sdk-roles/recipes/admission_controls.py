"""Actual closed configure/CLI refusal controls;never edits measured SDK inputs."""
from pathlib import Path
import hashlib,json,subprocess,sys
R=Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC/preview/native';D=Path(__file__).resolve().parent
SDK=R/'build/tag-rate-cross-owner-20261121/sdk-install';RT=N/'engine/runtime-binding';PINS=json.loads((D/'closed-role-pins.json').read_text())
O=D/'admission-controls';O.mkdir(exist_ok=True);rows=[]
manifest=RT/'provider-shared-quota-all-roles-runtime.json';approval=RT/'provider-shared-quota-all-roles-approval.json'
base=['python3','-I','-B',str(RT/'verify_runtime_manifest_shared_roles.py'),'--manifest',str(manifest),'--approval',str(approval),'--approval-sha256',PINS['all-roles-approval.json'],'--library',str(SDK/'lib/libtorrent-rasterbar.so.2.0.12'),'--include',str(SDK/'include')]
def run(name,args,expected):
 result=subprocess.run(args,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=45)
 log=O/(name+'.log');log.write_text(result.stdout)
 assert result.returncode!=0 and expected in result.stdout,(name,result.returncode,result.stdout[-1200:])
 rows.append(dict(name=name,exit=result.returncode,expected=expected,log_sha256=hashlib.sha256(log.read_bytes()).hexdigest()))
for name,option,value,reason in [
 ('old-manifest','--manifest',str(RT/'provider-public-shared-quota-runtime.json'),'Unknown all-role SDK manifest'),
 ('old-library','--library',str(R/'build/tag-rate-capacity-20261118/sdk-install/lib/libtorrent-rasterbar.so.2.0.12'),'Shared SDK role admission rejected:'),
 ('old-headers','--include',str(R/'build/tag-rate-capacity-20261118/sdk-install/include'),'Shared SDK role admission rejected:'),
 ('approval-digest','--approval-sha256','0'*64,'Unknown all-role SDK approval')]:
 argv=base.copy();argv[argv.index(option)+1]=value;header=O/(name+'.hpp');receipt=O/(name+'.json')
 run(name,argv+['--header',str(header),'--receipt',str(receipt)],reason);assert not header.exists() and not receipt.exists()
name='optimized-python';header=O/(name+'.hpp');receipt=O/(name+'.json');argv=base.copy();argv.insert(1,'-O');run(name,argv+['--header',str(header),'--receipt',str(receipt)],'requires enabled assertions');assert not header.exists() and not receipt.exists()
project=O/'source';project.mkdir(exist_ok=True)
(project/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.25)\nproject(RoleRefusal LANGUAGES NONE)\nadd_library(LibtorrentRasterbar::torrent-rasterbar SHARED IMPORTED)\nset_target_properties(LibtorrentRasterbar::torrent-rasterbar PROPERTIES IMPORTED_LOCATION "'+str(SDK/'lib/libtorrent-rasterbar.so.2.0.12')+'" INTERFACE_INCLUDE_DIRECTORIES "'+str(SDK/'include')+'")\ninclude("'+str(RT/'PersistentRuntimeBinding.cmake')+'")\n')
flags=dict(BIGLYBTC_SHARED_QUOTA_ALL_ROLES='ON',BIGLYBTC_PERSISTENT_SHARED_QUOTA_RUNTIME='ON',BIGLYBTC_PERSISTENT_TAG_RATE_RUNTIME='ON',BIGLYBTC_PERSISTENT_PUBLIC_CHAT_RUNTIME='ON',BIGLYBTC_PERSISTENT_MIXED_I2P_RUNTIME='ON',BIGLYBTC_PRIVATE_I2P_PROVIDER_OWNER='ON',BIGLYBTC_PERSISTENT_COEXISTENCE_RUNTIME='OFF',BIGLYBTC_PERSISTENT_RUNTIME_MANIFEST=str(manifest),BIGLYBTC_PERSISTENT_MIXED_I2P_APPROVAL=str(approval),BIGLYBTC_PRIVATE_I2P_OWNER_APPROVAL=str(approval),BIGLYBTC_PERSISTENT_MIXED_I2P_APPROVAL_SHA256=PINS['all-roles-approval.json'],BIGLYBTC_PRIVATE_I2P_OWNER_APPROVAL_SHA256=PINS['all-roles-approval.json'])
cases=[]
for key in ['BIGLYBTC_PERSISTENT_SHARED_QUOTA_RUNTIME','BIGLYBTC_PERSISTENT_TAG_RATE_RUNTIME','BIGLYBTC_PERSISTENT_PUBLIC_CHAT_RUNTIME','BIGLYBTC_PERSISTENT_MIXED_I2P_RUNTIME','BIGLYBTC_PRIVATE_I2P_PROVIDER_OWNER']:
 cases.append(('no-'+key.lower(),{key:'OFF'},'require the complete exact'))
cases += [('legacy-coexistence',{'BIGLYBTC_PERSISTENT_COEXISTENCE_RUNTIME':'ON'},'require the complete exact'),('empty-manifest',{'BIGLYBTC_PERSISTENT_RUNTIME_MANIFEST':''},'require the complete exact'),('different-owner-approval',{'BIGLYBTC_PRIVATE_I2P_OWNER_APPROVAL':str(RT/'provider-public-tag-rates-runtime.json')},'require one exact approval'),('without-all-role-flag',{'BIGLYBTC_SHARED_QUOTA_ALL_ROLES':'OFF'},'Shared chat cohort is Public-only')]
for name,overrides,reason in cases:
 cache=O/('cmake-'+name);args=['cmake','-S',str(project),'-B',str(cache),'-G','Ninja']+['-D'+k+'='+v for k,v in {**flags,**overrides}.items()]
 run(name,args,reason);assert not (cache/'runtime-binding/persistent_runtime_config.hpp').exists()
(D/'admission-control-receipt.json').write_text(json.dumps(rows,indent=2)+'\n')
print('PASS_SHARED_ROLE_ADMISSION_REFUSALS',len(rows),'cases;no candidate header or receipt emitted')
