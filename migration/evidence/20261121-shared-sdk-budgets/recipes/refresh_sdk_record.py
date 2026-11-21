from pathlib import Path
import difflib
import hashlib
import json
import subprocess

D=Path(__file__).resolve().parent
N=Path('/home/yurii/Java/BiglyBTC')
parent_path=N/'preview/native/engine/runtime-binding/provider-public-tag-rates-runtime.json'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
canonical=lambda value:hashlib.sha256(json.dumps(value,sort_keys=True,separators=(',',':')).encode()).hexdigest()
assert sha(parent_path)=='774c4f7521a7e01fae96a3cc44f73ebf08c35fd6584114f1581d6878f6fecddf'
parent=json.loads(parent_path.read_text())
source=D/'sdk-source'
inputs={str(p.relative_to(source)):sha(p) for p in sorted(source.rglob('*')) if p.is_file()}
assert len(inputs)==1102
assert inputs==json.loads((D/'current-quota-inputs.json').read_text())
chunks=[]
changed=[]
for name in sorted(set(parent['source_inputs_sha256'])|set(inputs)):
    previous=Path(parent['source'])/name
    current=source/name
    if name in parent['source_inputs_sha256']:
        assert sha(previous)==parent['source_inputs_sha256'][name],name
    if inputs.get(name)==parent['source_inputs_sha256'].get(name):
        continue
    changed.append(name)
    chunks.extend(difflib.unified_diff(
        previous.read_text().splitlines(True) if previous.exists() else [],
        current.read_text().splitlines(True) if current.exists() else [],
        fromfile='a/'+name if previous.exists() else '/dev/null',
        tofile='b/'+name if current.exists() else '/dev/null'))
assert len(changed)==18
patch=D/'sdk-shared-quota.patch'
patch.write_text(''.join(chunks))
include=D/'sdk-install/include'
headers={str(p.relative_to(include)):sha(p) for p in sorted((include/'libtorrent').rglob('*')) if p.is_file()}
assert len(headers)==290
for name,digest in headers.items():
    assert inputs['include/'+name]==digest,name
artifact=D/'sdk-install/lib/libtorrent-rasterbar.so.2.0.12'
exports=parent['required_exports']+[
    'biglybtc_shared_quota_features_v1','biglybtc_shared_quota_scope_create_v1',
    'biglybtc_shared_quota_scope_close_v1','biglybtc_shared_quota_scope_release_v1',
    'biglybtc_shared_quota_acquire_v1','biglybtc_shared_quota_set_v1','biglybtc_tag_shared_bind_v1']
symbols=subprocess.check_output(['readelf','--dyn-syms','--wide',str(artifact)],text=True)
available={s.split()[-1].split('@')[0] for s in symbols.splitlines() if len(s.split())>=8 and s.split()[6]!='UND'}
assert len(exports)==len(set(exports))==59 and set(exports)<=available
record=dict(schema='BIGLYBTC_PUBLIC_SHARED_QUOTA_RUNTIME_V1',roles=['Public'],
    allocator_binding='ONE_REVIEWED_SDK_IMAGE_ALL_ADMITTED_SESSIONS',
    parent_record=str(parent_path),parent_record_sha256=sha(parent_path),
    source=str(source),source_inputs_sha256=inputs,source_inventory_sha256=canonical(inputs),
    include=str(include),headers_sha256=headers,sdk_tree_sha256=canonical(headers),
    artifact=str(artifact),artifact_sha256=sha(artifact),artifact_bytes=artifact.stat().st_size,
    class_abi='0x4242435300020002',shared_quota_abi='0x4242515300010001',
    sdk_tick_interval_default_milliseconds=500,shared_planning_quantum_milliseconds=100,
    required_exports=exports,sdk_upstream_commit='740a0b9aeabe00e762cc0efe4a0f27593db2550b',
    upstream_identity_basis='Inherited exact parent archive/source attribution;not a fresh Git-tree comparison',
    patch=str(patch),patch_sha256=sha(patch))
manifest=D/'public-shared-quota-runtime.json'
manifest.write_text(json.dumps(record,indent=2)+'\n')
(D/'patch-receipt.json').write_text(json.dumps(dict(
    parent_manifest_sha256=sha(parent_path),parent_headers_sha256=parent['sdk_tree_sha256'],
    parent_source_inputs_sha256=canonical(parent['source_inputs_sha256']),
    patch_sha256=sha(patch),changed=changed,source_files=len(inputs)),indent=2)+'\n')
print(json.dumps(dict(manifest_sha256=sha(manifest),artifact_sha256=record['artifact_sha256'],
    artifact_bytes=record['artifact_bytes'],headers_sha256=record['sdk_tree_sha256'],
    source_inventory_sha256=record['source_inventory_sha256'],patch_sha256=sha(patch)),indent=2))
