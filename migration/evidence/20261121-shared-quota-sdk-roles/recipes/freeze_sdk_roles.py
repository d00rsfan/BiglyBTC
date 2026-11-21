"""Freeze measured SDK role checks;does not activate Native private tag rates."""
from pathlib import Path
import hashlib,json,re,subprocess,os,xml.etree.ElementTree as ET
R=Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';D=Path(__file__).resolve().parent;W=R/'build/public-nat-20261114'
S=R/'build/tag-rate-cross-owner-20261121';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def write(path,data):path.write_text(json.dumps(data,indent=2)+'\n')
sdk=S/'sdk-install/lib/libtorrent-rasterbar.so.2.0.12'
assert sha(sdk)=='05ce92d16e68c4c2f7046ff615dc762bef6d3d012b896bb1915025284ce62f3e'
roles=['Public','DedicatedPrivateI2pV1','PrivateI2pMulti','PublicPrivateCoexistence','MixedI2pTransportV1']
cases=['same-image-34-exports-and-markers','distinct-roles-public-and-private-admission-denials','loopback-tracker-peer-transfer-survives-private-withdraw','private-mixed-settings-failure-is-session-local','public-join-preserves-private-registration-and-independent-join','joined-public-disk-bytes-exact']
xmls={
 'provider-tests.xml':['test_biglybtc_mainline_identity','test_biglybtc_private_i2p_owner','test_biglybtc_swarm','test_biglybtc_tracker_response'],
 'wire-tests.xml':['id_enforcement_v4','id_enforcement_v6','parse_i2p_peers'],
 'mainline-tests.xml':['mainline-port-bridge'],'socks4a-tests.xml':['socks4a-provider'],'roles-generated/tests.xml':cases}
for name,names in xmls.items():
 rows=list(ET.parse(D/name).getroot().iter('testcase'));assert len(rows)==len(names) and {r.get('name') for r in rows}==set(names)
 assert all(r.get('status')=='run' and not any(r.find(k) is not None for k in ['failure','error','skipped']) for r in rows),name
assert re.findall(r'Totals: (\d+) passed, (\d+) failed, (\d+) skipped',(D/'socks4a-tests.xml').read_text())==[('6','0','0')]
env=dict(os.environ,LD_LIBRARY_PATH=str(sdk.parent))
listed=subprocess.check_output([str(D/'sdk-private-tests'),'--list'],env=env,text=True)
(D/'private-unit-list.log').write_text(listed)
actual={s[3:].rsplit('.',1)[-1] for s in listed.splitlines() if s.startswith(' - ')}
expected=set(re.findall(r'^TORRENT_TEST\(([^)]+)\)',(S/'sdk-source/test/test_biglybtc_private_i2p_owner.cpp').read_text(),re.M))
assert actual==expected and len(actual)==23
checks=['shared-roles-provider-suites','shared-roles-fresh-six-controls-r2','shared-roles-mainline-tests','shared-roles-socks4a-tests','shared-roles-wire-methods']+['shared-roles-quota-'+v for v in ['gcc','san','clang']]
for variant in ['gcc','san','clang']:
 log=(W/('shared-roles-quota-'+variant+'.log')).read_text();assert 'PASS_SHARED_ROLE_QUOTA 39 assertions' in log and not re.search(r'AddressSanitizer:|runtime error:',log)
inputs=[D/'role_quota_controls.cpp',D/'CMakeLists.txt',D/'link_sdk_private.py',D/'sdk-private-link-receipt.json',D/'sdk-role-source-review.json',D/'private-unit-list.log']
for sub in ['roles','mainline-fixture','socks4a-fixture']:
 inputs += [p for p in (D/sub).iterdir() if p.is_file()]
inputs += [D/name for name in xmls]+[W/(name+'.log') for name in checks]
images=[sdk,D/'sdk-private-tests',D/'roles-gcc/sdk-role-controls',D/'mainline-gcc/owned-mainline-port-proof',D/'socks4a-gcc/biglybtc-socks4a-provider-tests']
images += [S/('sdk-build/test/'+name) for name in ['test_biglybtc_mainline_identity','test_biglybtc_swarm','test_biglybtc_tracker_response','test_dht','test_tracker']]
images += [D/('quota-'+v+'/role-quota') for v in ['gcc','san','clang']]
history=json.loads((D/'sdk-role-source-review.json').read_text())
dispositions={}
for name in history['candidate_delta']:
 if 'http_tracker_connection' in name:purpose='Original HTTP response-field decoding inherited from reviewed Public SDK;owned I2P request admission remains set before HTTP construction;fresh private late-result/SAM controls and tracker parser pass'
 elif 'public_udp' in name or 'aux_/session_impl.hpp' in name:purpose='Inherited Public shared-UDP observer additions;actual new ABI Native IPv4/IPv6 checks and fresh private/mixed role controls pass'
 else:purpose='Previously qualified tag-class capacity,directional gating or common allocator delta;fresh actual private/mixed registration and common role-class controls pass'
 dispositions[name]=purpose
history['candidate_delta_dispositions']=dispositions;history['historical_source_drift_disposition']='Old checkout remains untouched and is not used as an executable source oracle. This new role closure uses the exact verified candidate sources and freshly rebuilt actual SDK tests.'
write(D/'sdk-role-source-review.json',history)
proof=dict(schema='BIGLYBTC_SHARED_QUOTA_SDK_ROLES_PROOF_V1',status='SDK_ROLES_REQUALIFIED_NATIVE_PENDING',roles=roles,artifact_sha256=sha(sdk),source_inventory_sha256='4859f8aadce41c54e91875d1ccda71cb52beb656267e8f3a55f91757375f5ec4',actual_ctest_suites=9,private_owner_methods=23,fresh_separate_role_cases=cases,common_role_quota_assertions_per_variant=39,common_role_quota_variants=['GCC16','scoped GCC16 ASan/UBSan with Release SDK','Clang24 with GCC16 libstdc++'],inputs_sha256={str(p):sha(p) for p in inputs},images_sha256={str(p):sha(p) for p in images},xml_cases=xmls,positive_runtime_groups=checks,private_tag_payload_qualified=False,native_private_profile_authority=False,full_sdk_sanitized=False,tsan=False)
write(D/'sdk-roles-proof.json',proof)
public=N/'preview/native/engine/runtime-binding/provider-public-shared-quota-runtime.json';m=json.loads(public.read_text());assert sha(public)=='f5490f29a74c6b63bc4f66e6c9dabb6fb2d4cfc6b267dccd7d1cc01d5c249081'
manifest={**m,'schema':'BIGLYBTC_SHARED_QUOTA_ALL_ROLES_RUNTIME_V1','roles':roles,'parent_public_manifest':str(public),'parent_public_manifest_sha256':sha(public),'sdk_role_proof':str(D/'sdk-roles-proof.json'),'sdk_role_proof_sha256':sha(D/'sdk-roles-proof.json'),'source_capsule_kind':'complete_candidate_source_inventory','source_capsule_sha256':m['source_inventory_sha256'],'private_tag_payload_qualified':False,'native_qualification_pending':True}
write(D/'all-roles-runtime.json',manifest)
approval=dict(schema='BIGLYBTC_SHARED_QUOTA_ALL_ROLES_APPROVAL_V1',status='EXACT_SDK_ROLES_NATIVE_QUALIFICATION_ONLY',roles=roles,runtime_manifest=str(N/'preview/native/engine/runtime-binding/provider-shared-quota-all-roles-runtime.json'),runtime_manifest_sha256=sha(D/'all-roles-runtime.json'),source_capsule_sha256=m['source_inventory_sha256'],artifact_sha256=sha(sdk),artifact_bytes=sdk.stat().st_size,sdk_tree_sha256=m['sdk_tree_sha256'],headers_sha256=m['headers_sha256'],source_build_record_sha256=sha(D/'sdk-roles-proof.json'),review_sha256=sha(D/'sdk-role-source-review.json'),shipping_approved=False,private_tag_payload_qualified=False)
write(D/'all-roles-approval.json',approval)
write(D/'closed-role-pins.json',{name:sha(D/name) for name in ['all-roles-runtime.json','all-roles-approval.json','sdk-roles-proof.json','sdk-role-source-review.json']})
print('Frozen actual SDK qualification:9 suites,23 private methods,6 role cases,39 controls per GCC/SAN/Clang;Native and I2P tag payload remain pending')
