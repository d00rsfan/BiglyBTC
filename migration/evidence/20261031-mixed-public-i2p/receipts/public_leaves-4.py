#!/usr/bin/env python3
"""Update only the existing optional preview after committed mixed39 qualification."""
import argparse,hashlib,json,os,re,shutil,subprocess,sys,tempfile
from pathlib import Path
R=Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';T=R/'build/mixed-network-20261031'
CACHE=R/'build/i2p-provider-attachment-20261029/native-followup-20261030/caches/gcc'
SAN=CACHE.parent/'san'
PRIOR='66a2d897fe98ddc9e93e20bf68b9d498891658257973c4843572b4e24cd3b97d'
DEFAULT='aad928753cb8f2dac8f99245298d987cfe2c4bee9e7bdebf5b8d7645ed6d559b'
RUNTIME='03694052165fab605b7f9a8c30ec9ddbb806b0d94a7eef642453f1341d838fe9'
APPROVAL='c47c93f8f2e90ef573d4e79ac7c8fb14589d0a3c3ce2e99db5be3f2577f41993'
RECORD='f1c09a9a9b097c226bf9261f8c17c6523a48f0f53aea0f14e3ae39034cf94e16'
SOURCE='05a9fad2ad069fb0a4017e6253c91da1b56f7bec67b6fe514194007a126b0ee0'
PROVIDER='dd71c87d9030a1db9f39741850bb56d989f87d409044fa693733b5352f01ed34'
REVIEW='bf16e1b8b78277a6d05fb6570bbef4fa63401000f37babf55bcdbfeedfea64ce'
TARGET='biglybtc-mixed-network-host-transfer-probe'
DRIVER='biglybtc-provider-mixed-runtime-deployment-driver'
def sha(p):
 h=hashlib.sha256()
 with Path(p).open('rb') as f:
  while data:=f.read(1024*1024):h.update(data)
 return h.hexdigest()
def load(p):return json.loads(Path(p).read_bytes())
def dump(p,v):Path(p).write_text(json.dumps(v,indent=2)+'\n')
def git(*args):return subprocess.check_output(['git','-C',str(N),*args],text=True).strip()
def inventory(root):
 for p in root.rglob('*'):
  if p.is_symlink():assert p.resolve(strict=True).is_relative_to(root),p
 return {p.relative_to(root).as_posix():sha(p) for p in sorted(root.rglob('*')) if p.is_file() and p!=root/'BUILD_MANIFEST.json'}
def checked_build(path,pin,lane,cid,tree):
 assert path.is_absolute() and sha(path)==pin;v=load(path)
 assert v['status']=='PASS' and v['lane']==lane and v['source_commit']==cid and v['source_tree']==tree
 assert Path(v['cache']).resolve(strict=True)==(CACHE if lane=='private' else SAN)
 saved=load(path.parent/'source-before.json');assert sha(path.parent/'source-before.json')==v['source_map_sha256']
 assert saved==load(path.parent/'source-after.json')
 assert set(saved)==set(git('ls-tree','-r','--name-only',cid,'--','preview/native').splitlines())
 assert all(sha(N/name)==h for name,h in saved.items())
 assert v['commands'] and all(x['exit_code']==0 and not x.get('timed_out',False) and sha(x['log'])==x['log_sha256'] for x in v['commands'])
 for row in v['images'].values():assert sha(row['path'])==row['sha256'] and Path(row['path']).stat().st_size==row['bytes']
 assert TARGET in v['images'] and 'BiglyBTC' in v['images'];return v,saved

def checked_acceptance(path,pin,lane,cid,tree,gcc_build_sha):
 assert path.is_absolute() and sha(path)==pin;run=path.parent;v=load(path);inputs=load(run/'inputs.json')
 assert v['status']=='PASS_REAL_ISOLATED_ROUTER_SINGLE_TORRENT_PUBLIC_I2P_MIXED' and v['native']['status']=='PASS'
 assert v['discovery_or_remote_tunnels_qualified'] is False and v['default_backend_activated'] is False
 assert inputs['source_commit']==cid and inputs['source_tree']==tree
 assert inputs['mixed_runtime_manifest_sha256']==RUNTIME and inputs['mixed_role_approval_sha256']==APPROVAL
 assert inputs['provider_record_sha256']==RECORD and inputs['provider_source_sha256']==SOURCE
 assert inputs['provider_artifact_sha256']==PROVIDER and inputs['provider_approval_sha256']==APPROVAL
 build=Path(inputs['build_receipt']);receipt,_=checked_build(build,inputs['build_receipt_sha256'],lane,cid,tree)
 if lane=='private':assert inputs['build_receipt_sha256']==gcc_build_sha
 assert inputs['images']==receipt['images'] and inputs['probe']==receipt['images'][TARGET]
 after=load(run/'inputs-after.json');assert after=={'status':'UNCHANGED','source_commit':cid,'inputs_sha256':sha(run/'inputs.json')}
 assert load(run/'private-cleanup.json')=={'owned_private_fixture_paths_removed':True,'subject_keys_published':False}
 assert load(run/'namespace-exit.json')=={'exit_code':0,'parent_descriptors_unchanged':True}
 rows=load(run/'commands.json');assert len(rows)==3 and {x['label'] for x in rows}=={'router','tracker','native'} and all(x['exit_code']==0 for x in rows)
 assert load(run/'native-loaded.json')['status']=='PASS' and load(run/'native-loaded.json')['provider_sha256']==PROVIDER
 assert all(v['native'][key] for key in ('one_mixed_library_row','mixed_single_torrent_both_source_ranges_verified','I2P_progress_without_Public_source','Public_progress_after_mixed_router_EOF','cumulative_library_traffic_preserved','joined_before_disk_read'))
 public=load(run/'public-artifacts.json')
 for leaf in ('result.json','inputs.json','inputs-after.json','commands.json','native-result.json','native-loaded.json','private-cleanup.json','namespace-exit.json','fence-before.json','fence-after.json','tracker-result.json','router-result.json'):
  assert public[str((run/leaf).resolve())]==sha(run/leaf)
 assert all(Path(name).is_absolute() and Path(name).parent==run and sha(name)==h for name,h in public.items())
 return {'path':str(path),'sha256':pin,'inputs_sha256':sha(run/'inputs.json'),'public_artifacts_sha256':sha(run/'public-artifacts.json'),'build_receipt_sha256':inputs['build_receipt_sha256']}

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source-commit',required=True)
 for name in ('build-receipt','gcc-acceptance','san-acceptance'):
  p.add_argument('--'+name,required=True,type=Path);p.add_argument('--'+name+'-sha256',required=True)
 p.add_argument('--previous-manifest-sha256',required=True);p.add_argument('--output',required=True,type=Path);a=p.parse_args()
 assert re.fullmatch('[0-9a-f]{40}',a.source_commit) and git('rev-parse','HEAD')==a.source_commit
 assert not git('status','--porcelain','--untracked-files=all','--','preview/native')
 tree=git('rev-parse','HEAD:preview/native');gcc,saved=checked_build(a.build_receipt,a.build_receipt_sha256,'private',a.source_commit,tree)
 proofs=[checked_acceptance(a.gcc_acceptance,a.gcc_acceptance_sha256,'private',a.source_commit,tree,a.build_receipt_sha256),checked_acceptance(a.san_acceptance,a.san_acceptance_sha256,'san',a.source_commit,tree,a.build_receipt_sha256)]
 assert a.gcc_acceptance.parent!=a.san_acceptance.parent,'Distinct fresh lane subjects required'
 runtime_path=Path('/home/yurii/Java/BiglyBT/build/mixed-network-20261031/sdk-role-integration/accept-rearm-r2/runtime-manifest.json');role_path=Path('/home/yurii/Java/BiglyBT/build/mixed-network-20261031/sdk-role-integration/accept-rearm-r2/root-mixed-role-approval.json');record_path=Path('/home/yurii/Java/BiglyBT/build/mixed-network-20261031/provider/source-build-record-accept-rearm.json');review_path=Path('/home/yurii/Java/BiglyBT/build/mixed-network-20261031/sdk-role-integration/accept-rearm-r2/root-artifact-review.json')
 source_path=Path('/home/yurii/Java/BiglyBT/build/mixed-network-20261031/provider/amendments/mixed-accept-rearm/amendments/steady-accept-backoff/manifest.json')
 for path,pin in ((runtime_path,RUNTIME),(role_path,APPROVAL),(record_path,RECORD),(source_path,SOURCE),(review_path,REVIEW)):assert sha(path)==pin
 runtime,approval,record=load(runtime_path),load(role_path),load(record_path);artifact=Path(runtime['artifact'])
 assert approval['shipping_approved'] is False and runtime['shipping_approved'] is False
 assert artifact.is_absolute() and artifact.stat().st_size==10022696 and sha(artifact)==PROVIDER
 assert runtime['source_build_record_sha256']==RECORD and runtime['source_capsule_sha256']==SOURCE and len(runtime['required_exports'])==39
 for target in ('BiglyBTC',DRIVER):
  row=gcc['images'][target];assert Path(row['path'])==CACHE/('app' if target=='BiglyBTC' else 'tests')/target
 default,dest=N/'dist',N/'dist-i2p';assert dest.is_dir() and not dest.is_symlink() and dest.resolve()==dest
 assert sha(default/'BUILD_MANIFEST.json')==DEFAULT;default_before=inventory(default)
 prior=dest/'BUILD_MANIFEST.json';assert a.previous_manifest_sha256==PRIOR==sha(prior)
 before=inventory(dest);assert before==load(prior)['files_sha256'] and len(before)==119
 links={str(q.relative_to(dest)):os.readlink(q) for q in dest.rglob('*') if q.is_symlink()}
 provider_relative='usr/lib/biglybtc/lib/libtorrent-rasterbar.so.2.0.12';provider_dest=dest/provider_relative
 assert provider_dest.is_file() and not provider_dest.is_symlink()
 aliases={name for name in before if (dest/name).resolve()==provider_dest}
 assert len(aliases)==6 and all(before[name]=='7ffbd4c63d7770168bcbdf8e8c19644e18d1509d57ac148331a3c4bd094310c4' for name in aliases)
 for name in ('BiglyBTC-plugin-azlocprov.so','BiglyBTC-plugin-SpeedScheduler.so'):
  assert sha(CACHE/'app'/name)==sha(dest/'usr/lib/biglybtc/lib'/name),'Changed plugin requires separate staging review'
 a.output=a.output.resolve();assert a.output.is_relative_to(T/'preview-stage') and not a.output.exists();a.output.mkdir(parents=True)
 origin={'head':subprocess.check_output(['git','-C',str(R),'rev-parse','HEAD'],text=True).strip(),'index_sha256':sha(R/'.git/index')}
 subprocess.run([sys.executable,'-B',str(N/'preview/native/engine/runtime-binding/verify_runtime_manifest_mixed.py'),'--manifest',str(runtime_path),'--library',str(artifact),'--include',str(artifact.parents[1]/'include'),'--approval',str(role_path),'--approval-sha256',APPROVAL,'--header',str(a.output/'binding.hpp'),'--receipt',str(a.output/'binding.json')],check=True,timeout=180)
 assert (a.output/'binding.hpp').read_bytes()==(CACHE/'engine/runtime-binding/persistent_runtime_config.hpp').read_bytes()
 environment={k:v for k,v in os.environ.items() if k not in ('LD_PRELOAD','LD_AUDIT','LD_LIBRARY_PATH')}
 lookup=[dest/'usr/lib/biglybtc/lib',dest/'usr/lib/biglybtc/qt/lib',dest/'dependencies/libtorrent/usr/lib/x86_64-linux-gnu'];environment['LD_LIBRARY_PATH']=os.pathsep.join(map(str,lookup))
 # Existing copied deployment driver enforces an owned0700 /tmp root and actual
 # relocated39 mapping/FD/hash proof. These two temporary copies are removed now.
 with tempfile.TemporaryDirectory(prefix='biglybtc-runtime-deploy-',dir='/tmp') as temp:
  moved=Path(temp);(moved/'lib').mkdir();shutil.copy2(Path(gcc['images'][DRIVER]['path']),moved/'driver');shutil.copy2(artifact,moved/'lib/libtorrent-rasterbar.so.2.0.12');(moved/'lib/libtorrent-rasterbar.so.2.0').symlink_to('libtorrent-rasterbar.so.2.0.12')
  relocated=dict(environment,LD_LIBRARY_PATH=str(moved/'lib')+os.pathsep+environment['LD_LIBRARY_PATH'])
  smoke=subprocess.run([str(moved/'driver'),str(moved),'copy','bound'],env=relocated,capture_output=True,timeout=15)
  (a.output/'relocation.stdout').write_bytes(smoke.stdout);(a.output/'relocation.stderr').write_bytes(smoke.stderr)
  assert smoke.returncode==0 and b'proof=mixed39 retained_pin=1' in smoke.stdout and b'original_opens=0' in smoke.stdout
  linked=subprocess.check_output(['ldd',str(CACHE/'app/BiglyBTC')],env=relocated,text=True,timeout=30)
  assert 'not found' not in linked and str(moved/'lib/libtorrent-rasterbar.so.2.0') in linked
  for line in linked.splitlines():
   if '=>' not in line:continue
   value=line.split('=>',1)[1].split()[0]
   if value.startswith('/'):
    path=Path(value).resolve();assert not str(path).startswith(('/opt/Qt/',str(R/'build')))
    if str(path).startswith('/tmp/'):assert path==moved/'lib/libtorrent-rasterbar.so.2.0.12'
    if line.strip().startswith('libQt6'):assert path.is_relative_to(dest)
  (a.output/'prestage-ldd.log').write_text(linked)
 doc='usr/share/doc/biglybtc-experimental/private-i2p/'
 texts={'README.txt':'Optional native Public + I2P mixed desktop preview.\n\nRun ./BiglyBTC --profile ABSOLUTE_ISOLATED_PROFILE --i2p-router OWNED_I2CP_PORT. Explicit saved [Public,I2P] or [I2P,Public] uses one Public torrent/storage owner with a source-bound key0 I2P route; pure I2P retains its separate original key1 service. Existing --private-i2p remains private-only. Genuine saved identities/settings/metadata are required; none are bundled.\n\nQualified only with isolated loopback subjects and explicitly warmed, signature-verified local LS2 caches. Internet discovery, NAT, remote/multihop tunnels, default activation, complete plugin/backend parity and performance guarantees remain unqualified.\n',doc+'SCOPE.md':'Optional39-export same-image Public/private/mixed roles, one payload owner per mixed torrent, shared saved queue/rates and exact cumulative payload observations. Actual GCC and SAN fixtures verify complementary Public/I2P ranges, source-bound route retry, Public continuation after I2CP EOF, UI lifecycle and joined same-profile restart. Scope remains isolated loopback with explicitly warmed signed local LS2 caches; discovery/remote tunnels/performance/full parity remain open. No keys, profiles or ephemeral capabilities are bundled. shipping_approved remains false. root-private-owner-approval.json is the retained119-file layout name for the new unified mixed-role approval.\n'}
 copies={'usr/lib/biglybtc/bin/BiglyBTC':Path(gcc['images']['BiglyBTC']['path']),provider_relative:artifact,doc+'root-private-owner-approval.json':role_path,doc+'source-build-record.json':record_path}
 changed=set(texts)|set(copies)|aliases;assert changed<=set(before)
 for name in sorted(set(texts)|set(copies)):
  target=dest/name;assert not target.is_symlink();temporary=target.with_name(target.name+'.mixed-stage');assert not temporary.exists()
  if name in copies:shutil.copy2(copies[name],temporary)
  else:temporary.write_text(texts[name])
  os.replace(temporary,target)
 after=inventory(dest);assert set(after)==set(before) and len(after)==119
 assert all(after[name]==h for name,h in before.items() if name not in changed)
 assert all(after[name]==PROVIDER for name in aliases) and after['usr/lib/biglybtc/bin/BiglyBTC']==gcc['images']['BiglyBTC']['sha256']
 assert links=={str(q.relative_to(dest)):os.readlink(q) for q in dest.rglob('*') if q.is_symlink()}
 linked=subprocess.check_output(['ldd',str(dest/'usr/lib/biglybtc/bin/BiglyBTC')],env=environment,text=True,timeout=30)
 assert not any(x in linked for x in ('not found','/opt/Qt/','/tmp/',str(R/'build')))
 for name in ('libtorrent-rasterbar.so.2.0','libQt6Core.so.6','libQt6Network.so.6','libQt6Gui.so.6','libQt6Widgets.so.6'):
  line=next(x for x in linked.splitlines() if x.strip().startswith(name+' => '));assert Path(line.split('=>',1)[1].split()[0]).resolve().is_relative_to(dest)
 (a.output/'ldd.log').write_text(linked)
 manifest={'scope':'Optional one-owner Public+I2P mixed and separate pure-I2P sessions; warmed-cache loopback qualification only','source_commit':a.source_commit,'source_tree':tree,'build_receipt_sha256':a.build_receipt_sha256,'previous_manifest_sha256':PRIOR,'launch':'./BiglyBTC --profile ABSOLUTE_ISOLATED_PROFILE --i2p-router OWNED_I2CP_PORT','private_only_launch':'./BiglyBTC --profile ABSOLUTE_ISOLATED_PROFILE --private-i2p OWNED_I2CP_PORT','provider_sha256':PROVIDER,'provider_source_capsule_sha256':SOURCE,'provider_source_build_record_sha256':RECORD,'provider_artifact_review_sha256':REVIEW,'role_approval_sha256':APPROVAL,'role_approval_raw':role_path.read_text(),'runtime_manifest_sha256':RUNTIME,'runtime_manifest_raw':runtime_path.read_text(),'acceptance':proofs,'default_dist_preserved_sha256':DEFAULT,'files_sha256':after,'shipping_approved':False}
 temporary=prior.with_name('BUILD_MANIFEST.json.mixed-stage');assert not temporary.exists();dump(temporary,manifest);os.replace(temporary,prior)
 assert inventory(default)==default_before and sha(default/'BUILD_MANIFEST.json')==DEFAULT
 assert all(sha(N/name)==h for name,h in saved.items()) and git('rev-parse','HEAD')==a.source_commit
 assert origin=={'head':subprocess.check_output(['git','-C',str(R),'rev-parse','HEAD'],text=True).strip(),'index_sha256':sha(R/'.git/index')}
 checked_build(a.build_receipt,a.build_receipt_sha256,'private',a.source_commit,tree)
 for path,pin,lane in ((a.gcc_acceptance,a.gcc_acceptance_sha256,'private'),(a.san_acceptance,a.san_acceptance_sha256,'san')):checked_acceptance(path,pin,lane,a.source_commit,tree,a.build_receipt_sha256)
 result={'status':'PASS_STAGED_OPTIONAL_MIXED_PREVIEW','source_commit':a.source_commit,'source_tree':tree,'manifest_sha256':sha(prior),'mapped_files':119,'default_dist_unchanged':True,'shipping_approved':False,'build_receipt_sha256':a.build_receipt_sha256,'acceptance':proofs,'role_approval_sha256':APPROVAL,'provider_sha256':PROVIDER,'relocation_exit':smoke.returncode,'retained_same_image_exports':39,'ldd_sha256':sha(a.output/'ldd.log'),'files_changed':sorted(name for name in before if after[name]!=before[name]),'backup_or_archive_created':False}
 dump(a.output/'result.json',result);print(json.dumps(result))
if __name__=='__main__':main()
