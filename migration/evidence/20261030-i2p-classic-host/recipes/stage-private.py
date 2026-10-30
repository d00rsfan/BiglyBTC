from pathlib import Path
import shutil,subprocess,json,hashlib,os,sys
R=Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';T=R/'build/host-i2p-20261030';source=N/'dist';dest=N/'dist-i2p';cache=R/'build/i2p-provider-attachment-20261029/native-followup-20261030/caches/gcc'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
before=sha(source/'BUILD_MANIFEST.json');assert before==json.loads((T/'before-preservation.json').read_text())['dist_manifest_sha256']
if dest.exists():
 prior=json.loads((dest/'BUILD_MANIFEST.json').read_text());assert all(sha(dest/name)==digest for name,digest in prior['files_sha256'].items()),'Preview files changed outside staged build'
else:shutil.copytree(source,dest,symlinks=True)
for relative in ['BiglyBTC-preview','usr/bin/BiglyBTC-preview','usr/lib/biglybtc/bin/BiglyBTC-headless']:
 p=dest/relative
 if p.is_file() or p.is_symlink():p.unlink()
shutil.copy2(cache/'app/BiglyBTC',dest/'usr/lib/biglybtc/bin/BiglyBTC')
for name in ['BiglyBTC-plugin-azlocprov.so','BiglyBTC-plugin-SpeedScheduler.so','libmaxminddb.so.0','libarchive.so.13']:
 f=cache/'app'/name
 if f.is_file():shutil.copy2(f,dest/'usr/lib/biglybtc/lib'/name)
provider=R/'build/i2p-provider-attachment-20261029/provider-candidate-r2';record=json.loads((provider/'source-build-record.json').read_text());artifact=Path(record['artifact']);assert sha(artifact)==record['artifact_sha256']
for f in dest.rglob('libtorrent-rasterbar.so*'):
 if f.is_file():shutil.copyfile(artifact,f)
shutil.copy2(T/'README-preview-draft.txt',dest/'README.txt')
doc=dest/'usr/share/doc/biglybtc-experimental/private-i2p';doc.mkdir(parents=True,exist_ok=True)
for name in ['source-build-record.json','root-private-owner-approval.json']:shutil.copy2(provider/name,doc/name)
(doc/'SCOPE.md').write_text('Explicit optional private I2P preview: ordinary metainfo import/recovery, original saved pure identity/tunnel settings, classic Start/Pause/Resume/Stop/close and observed I2P Statistics. One active torrent/identity/profile, owned loopback router; no Public fallback. Public discovery/multihop, general queue/rate policy, multi-torrent and whole plugin/performance parity remain open.\n')
for f in dest.rglob('*'):
 if f.is_symlink():assert f.resolve().is_relative_to(dest),f
lookup=[dest/'usr/lib/biglybtc/lib',dest/'usr/lib/biglybtc/qt/lib',dest/'dependencies/libtorrent/usr/lib/x86_64-linux-gnu'];env=dict(os.environ,LD_LIBRARY_PATH=os.pathsep.join(map(str,lookup)));env.pop('LD_PRELOAD',None)
linked=subprocess.check_output(['ldd',str(dest/'usr/lib/biglybtc/bin/BiglyBTC')],env=env,text=True);(T/'bundle-ldd.log').write_text(linked)
assert all(x not in linked for x in ['not found','/opt/Qt/','/tmp/','/home/yurii/Java/BiglyBT/build/']),linked
manifest={'scope':'Explicit private I2P classic desktop preview; one original identity/torrent per profile; Public and multihop qualification remain open','source_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=N,text=True).strip(),'source_tree':subprocess.check_output(['git','rev-parse','HEAD:preview/native'],cwd=N,text=True).strip(),'build_receipt_sha256':sha(T/'private'/sys.argv[1]/'build-receipt.json'),'launch':'./BiglyBTC --profile ABSOLUTE_ISOLATED_PROFILE --private-i2p OWNED_I2CP_PORT','provider_sha256':sha(artifact),'provider_source_capsule_sha256':'9d264115b5964f2ed542d193697b77263f974f0d4c3f929e829bd1c4c1398671','default_dist_preserved_sha256':before,'files_sha256':{f.relative_to(dest).as_posix():sha(f) for f in sorted(dest.rglob('*')) if f.is_file() and f!=dest/'BUILD_MANIFEST.json'}}
(dest/'BUILD_MANIFEST.json').write_text(json.dumps(manifest,indent=2)+'\n')
ignore=N/'.gitignore';s=ignore.read_text();s+='\n/dist-i2p/\n' if '/dist-i2p/' not in s else '';ignore.write_text(s)
assert sha(source/'BUILD_MANIFEST.json')==before
receipt={'status':'PASS_STAGED_PRIVATE_PREVIEW','path':str(dest),'manifest_sha256':sha(dest/'BUILD_MANIFEST.json'),'mapped_files':len(manifest['files_sha256']),'ldd_sha256':sha(T/'bundle-ldd.log'),'default_manifest_preserved':True,'source_commit':manifest['source_commit']};(T/'bundle.json').write_text(json.dumps(receipt,indent=2)+'\n');print(json.dumps(receipt))
