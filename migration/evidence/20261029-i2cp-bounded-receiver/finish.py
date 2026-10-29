#!/usr/bin/env python3
"""One-shot receiver closeout after actual qualification; no build/test replay."""
import ctypes, importlib.util, json, os, pathlib, re, shutil, subprocess
import xml.etree.ElementTree as ET
R=pathlib.Path('/home/yurii/Java/BiglyBT'); N=R.parent/'BiglyBTC'; H=pathlib.Path(__file__).resolve().parent
E=pathlib.Path('migration/evidence/20261029-i2cp-bounded-receiver')
SOURCE='61d1b53b07be81be7a98eaabe40794d8185fbfbe'; ATTEMPT='r1'
ADOPTION_SHA='9e95a88fec1d0fcdf0cb4aadd678f699ddc4c1c296269a0737aa5332e70ccf8a'
OLD_DIST='bcbcb9b9db1acc546d437659cb40e2bdc8a30e57960e00d8ab7dd6582ca462a4'
spec=importlib.util.spec_from_file_location('private',H/'private.py'); v=importlib.util.module_from_spec(spec); spec.loader.exec_module(v); s=v.s

def copy_file(source,target): target.parent.mkdir(parents=True,exist_ok=True); shutil.copyfile(source,target)
def append_unique(values,items):
    for item in items:
        if item not in values: values.append(item)
def commands(root,result,names):
    assert s.sha(root/'commands.json')==result['commands_sha256']; rows=s.load(root/'commands.json')
    assert [row['name'] for row in rows]==names
    for row in rows: assert row.get('exit')==0 and 'exception' not in row and s.sha(row['log'])==row['log_sha256']

def main():
    assert not (H/'promotion.json').exists() and not (R/E).exists() and not (N/E).exists()
    assert s.sha(H/'root-adoption.json')==ADOPTION_SHA; adoption=s.load(H/'root-adoption.json')
    assert adoption['source_commit']==SOURCE and adoption['source_path_count']==9
    for name,row in adoption['source_paths'].items(): assert s.sha(N/name)==row['after_sha256']
    snapshot=s.source_snapshot(SOURCE); lanes={}; images={}
    for lane in ('gcc','clang','san'):
        root=H/'runs'/(lane+'-'+ATTEMPT); d=s.load(root/'result.json')
        assert d['status']=='PASS_BOUNDED_I2CP_RECEIVER_COHORT' and d['lane']==lane and d['source_commit']==SOURCE
        assert d['actual_suites']==5 and d['source_unchanged'] and d['images_unchanged'] and not d['historical_passes_pooled']
        assert d['runner_sha256']==s.sha(H/'run.py') and d['source_receipt_sha256']==ADOPTION_SHA
        assert s.load(root/'source-before.json')==snapshot and s.sha(root/'tests.xml')==d['tests_xml_sha256'] and s.sha(root/'ctest-full.log')==d['full_ctest_log_sha256']
        assert s.passed_xml(root/'tests.xml',v.batch.SUITES)==d['suite_names']
        assert all(row.get('status')=='run' for row in ET.parse(root/'tests.xml').getroot().iter('testcase'))
        assert not re.search(r'^(?:SKIP\s*:|FAIL!)',(root/'ctest-full.log').read_text(errors='replace'),re.M)
        commands(root,d,['build','tests']); images[lane]=s.load(root/'images-before.json')
        expected=set(v.batch.SUITES.values()) | (set(v.batch.PRODUCTS) if lane=='gcc' else set()); assert set(images[lane])==expected
        for image in images[lane].values(): assert s.sha(image['path'])==image['sha256'] and pathlib.Path(image['path']).stat().st_size==image['bytes']
        lanes[lane]={'result_sha256':s.sha(root/'result.json'),'actual_suites':5,'suite_names':d['suite_names'],'compiled_products':len(expected),'sanitizer_environment':d['sanitizer_environment']}
    privatepath=H/'private-runs'/ATTEMPT/'result.json'; private=s.load(privatepath)
    assert private['status']=='PASS_PRIVATE_BOUNDED_I2CP_RECEIVER' and private['source_commit']==SOURCE and private['source_unchanged'] and private['runner_sha256']==s.sha(H/'private.py')
    assert set(private['lane_inputs'])==set(lanes) and [row['suite'] for row in private['actual_tests']]==list(v.batch.SUITES)
    assert set(private['copied_images'])==set(v.batch.SUITES.values())
    for lane,row in private['lane_inputs'].items(): assert row['sha256']==s.sha(H/'runs'/(lane+'-'+ATTEMPT)/'result.json')
    commands(privatepath.parent,private,['stage',*[name for suite in v.batch.SUITES for name in ('loader-'+suite,suite)]])
    offlinepath=H/'offline-runs'/ATTEMPT/'result.json'; offline=s.load(offlinepath)
    assert offline['status']=='PASS_ACTUAL_RECEIVE_OBJECTS' and offline['source_commit']==SOURCE and offline['source_unchanged'] and offline['native_images_unchanged']
    assert offline['runner_sha256']==s.sha(H/'offline.py') and offline['gcc_result_sha256']==s.sha(H/'runs'/('gcc-'+ATTEMPT)/'result.json')
    assert offline['cases_per_reference_per_direction']==4 and set(offline['class_origins'])=={'official','modified'} and not offline['old_java_replays']
    commands(offlinepath.parent,offline,['receive-objects'])
    for item in [*adoption['source_capsules'],*adoption['source_reviews']]: assert s.sha(item['path'])==item['sha256']
    candidate=pathlib.Path(private['candidate']); assert not candidate.is_symlink() and candidate.resolve().parent==privatepath.parent.resolve()
    assert s.sha(candidate/'BUILD_MANIFEST.json')==private['candidate_manifest_sha256']; new=v.bundle(candidate); old=v.bundle(N/'dist')
    assert s.sha(N/'dist/BUILD_MANIFEST.json')==private['old_dist_unchanged']==OLD_DIST
    oldlock=(R/'migration/upstreams.lock.json').read_bytes(); assert (N/'migration/upstreams.lock.json').read_bytes()==oldlock
    lock=json.loads(oldlock); before=json.loads(oldlock); component=next(c for c in lock['components'] if c['id']=='services.i2cp_initial_session')
    append_unique(component['native_commits'],[{'repository':'https://github.com/d00rsfan/BiglyBTC.git','commit':SOURCE,'purpose':'integration'}])
    append_unique(component['review_records'],[(E/'REVIEW.md').as_posix()]); append_unique(component['evidence'],[(E/'verification.json').as_posix()])
    append_unique(component['target_paths'],adoption['source_paths'])
    coverage=next(c for c in component['source_coverage'] if c['upstream_id']=='i2p-java')
    pins=s.load(R/'build/i2cp-datagram-receive-20261029/source-pins.json'); append_unique(coverage['source_paths'],[row['path'] for row in pins['sources']])
    append_unique(coverage['source_symbols'],['ReceiveMessageBeginMessage','ReceiveMessageEndMessage','MessageStatusMessageHandler.handleMessage','MessagePayloadMessageHandler.handleMessage','I2PSessionImpl2.receiveMessage','ClientConnectionRunner.destinationEstablished','ClientMessageEventListener.handleReceiveBegin','ClientMessageEventListener.handleReceiveEnd'])
    for b,a in zip(before['components'],lock['components'],strict=True):
        if b['id']!='services.i2cp_initial_session': assert b==a; continue
        assert b['state']==a['state']=='in_progress'
        for bc,ac in zip(b['source_coverage'],a['source_coverage'],strict=True):
            for key in ('upstream_id','baseline_commit','first_ported_commit','integrated_through_commit','last_reviewed_commit'): assert bc[key]==ac[key]
    assert before['upstreams']==lock['upstreams'] and before['external_dependencies']==lock['external_dependencies'] and len(lock['external_dependencies'])==22
    out=R/E; out.mkdir(parents=True); artifacts={}
    def copy(path,name):
        path=pathlib.Path(path); assert path.is_file() and not path.is_symlink() and path.stat().st_size<10*1024*1024
        with path.open('rb') as stream: assert stream.read(4)!=b'\x7fELF'
        target=out/name; copy_file(path,target); artifacts[name]=s.sha(target)
    for name in ('adopt.py','approved-inputs.json','root-adoption.json','run.py','private.py','offline.py','finish.py'): copy(H/name,name)
    for attempt in sorted((H/'runs').iterdir()):
        if attempt.is_dir() and re.fullmatch(r'(gcc|clang|san)-[A-Za-z0-9_-]+',attempt.name):
            for leaf in attempt.iterdir():
                if leaf.is_file() and leaf.suffix in ('.json','.log','.xml'): copy(leaf,'runs/'+attempt.name+'/'+leaf.name)
    for leaf in privatepath.parent.iterdir():
        if leaf.is_file() and leaf.suffix in ('.json','.log','.xml','.txt'): copy(leaf,'private/'+leaf.name)
    for leaf in offlinepath.parent.rglob('*'):
        if leaf.is_file() and leaf.suffix!='.class': copy(leaf,'offline/'+str(leaf.relative_to(offlinepath.parent)))
    for i,item in enumerate(adoption['source_capsules']):
        cap=pathlib.Path(item['path']); copy(cap,'source/capsule-'+str(i)+'/manifest.json'); m=s.load(cap)
        for name,pin in m['artifacts_sha256'].items():
            assert s.sha(cap.parent/name)==pin
            if not name.startswith(('staging/','before/','original-source/')): copy(cap.parent/name,'source/capsule-'+str(i)+'/'+name)
    for i,item in enumerate(adoption['source_reviews']): copy(item['path'],'source/review-'+str(i)+'.json')
    copy(N/'dist/BUILD_MANIFEST.json','previous-dist-manifest.json')
    verification={'status':'PASS_BOUNDED_I2CP_DATAGRAM_RECEIVER','source_commit':SOURCE,'source_paths':9,'lanes':lanes,'private_commands':5,'actual_receive_cases_per_reference_per_direction':4,'provider_rebuilt':False,'provider_sha256':private['provider_sha256'],'historical_passes_pooled':False,'artifacts_sha256':artifacts,'promotion':None,'real_router_or_profile_used':False,'public_network_activated':False,'whole_feature_or_performance_parity_claimed':False}
    s.write(out/'verification.json',verification)
    (out/'REVIEW.md').write_text(f'''# Bounded explicit I2CP datagram receiver

Qualified source `{SOURCE}` changes9 exact paths: seven receiver leaves and two CMake compositions. Five affected suites passed freshly on GCC/Clang/leak-enabled ASan+UBSan; GCC builds8 products including app/headless/smoke, other lanes5 tests only. Private runtime runs the exact five GCC suite commands with actual loaded private libraries. Each pinned Java artifact produces four actual Begin/End/Available/Payload plus Datagram2/3 cases, verified by current native code and exported back for Java verification. Actual labels/class origins/current ELF/source bind the receipt. No old send/status/LS2/session/field Java replay or historic PASS pooling.

Explicit enable requires an owned signer; admission additionally requires current connected session, same full accepted Destination and unexpired actual LS2 enqueue. CREATED is insufficient. Immutable initial fastReceive follows the raw original router predicate, not the Java client's injected default. Legacy Available has one independent absolute fetch deadline; Begin is written before matching Payload admission, and End releases router storage before application verification/consumption. Disabled/full legacy receivers may release with End without Begin, supported by the exact original handler. Fast mode sends no legacy controls. Signed Datagram2 verifies sender-key possession/current recipient, not freshness or every raw Destination byte; Datagram3 remains explicitly unsigned claimed data.

Four unread datagrams/160KiB decoded wire+key+hash+payload, one fetch, four queued controls/one write and finite recent64 are bounded. Unread IDs retain duplicate protection beyond ring rotation. Corrupt inner gzip/signature/recipient/protocol is dropped without inventing delivery or destroying valid outbound work; outer framing/resource/transport remains terminal. Generation-tagged coalesced queued callbacks permit pop/disable/close/restart/delete. End is storage release, not app acceptance. Queue budget excludes documented bounded transient buffers/container metadata. Sticky controlLost permits incomplete storage release; no actual OS-level stalled-socket overflow experiment is claimed.

No real router, profile, listener/backend/Host/default/Streaming/public activation, OPEN/tunnel health or end-to-end delivery qualification. Modified whole-core/source equivalence remains open. Existing services.i2cp_initial_session stays in_progress; all baseline/null fields, other107 components and22 dependencies remain unchanged. Any earlier failed attempts present in this batch are retained separately with no final PASS credit. Promotion and both proposed-registry preflights are recorded in verification.json.
''')
    shutil.copytree(out,N/E)
    proposed=H/'proposed-upstreams.lock.json'; assert not proposed.exists(); s.write(proposed,lock); validation={}
    code='import json,pathlib,runpy,sys; runpy.run_path(sys.argv[1])["validate"](json.loads(pathlib.Path(sys.argv[2]).read_bytes()))'
    for root in (R,N):
        log=H/('registry-validation-'+root.name+'.log'); cmd=['python3','-B','-c',code,str(root/'migration/tools/validate.py'),str(proposed)]
        with log.open('wb') as stream: done=subprocess.run(cmd,cwd=root,env=dict(os.environ,GIT_NO_LAZY_FETCH='1',GIT_OPTIONAL_LOCKS='0'),stdout=stream,stderr=subprocess.STDOUT)
        validation[root.name]={'command':cmd,'exit':done.returncode,'log_sha256':s.sha(log),'proposed_lock_sha256':s.sha(proposed)}
        copy(log,log.name); copy_file(log,N/E/log.name); assert done.returncode==0,log
    for proc in pathlib.Path('/proc').glob('[0-9]*/maps'):
        try: maps=proc.read_text()
        except (OSError,PermissionError): continue
        assert str(N/'dist')+'/' not in maps and str(privatepath.parent)+'/' not in maps,'A preview/test image is still running'
    assert s.source_snapshot(SOURCE)==snapshot and v.bundle(candidate)==new and v.bundle(N/'dist')==old
    for lane in images.values():
        for image in lane.values(): assert s.sha(image['path'])==image['sha256']
    assert all((root/'migration/upstreams.lock.json').read_bytes()==oldlock for root in (R,N))
    libc=ctypes.CDLL(None,use_errno=True); exchange=libc.renameat2; exchange.argtypes=[ctypes.c_int,ctypes.c_char_p,ctypes.c_int,ctypes.c_char_p,ctypes.c_uint]
    assert exchange(-100,os.fsencode(N/'dist'),-100,os.fsencode(candidate),2)==0,os.strerror(ctypes.get_errno())
    assert v.bundle(N/'dist')==new and v.bundle(candidate)==old; shutil.rmtree(candidate)
    for target,image in private['copied_images'].items():
        copied=privatepath.parent/target; assert s.sha(copied)==image['sha256']; copied.unlink()
    shutil.rmtree(privatepath.parent/'test-support')
    promotion={'status':'PROMOTED_ONE_CHECKED_DEVELOPMENT_DIST','source_commit':SOURCE,'manifest_sha256':s.sha(N/'dist/BUILD_MANIFEST.json'),'leaf_count':len(new['files_sha256']),'previous_manifest_sha256':OLD_DIST,'previous_dist_removed':True,'atomic_exchange':True,'copied_test_images_removed':5,'test_support_removed':True,'both_registry_preflights_passed':True}
    s.write(H/'promotion.json',promotion); copy(H/'promotion.json','promotion.json'); copy(N/'dist/BUILD_MANIFEST.json','dist-manifest.json'); copy(proposed,'proposed-upstreams.lock.json')
    verification.update(promotion=promotion,registry_validation=validation,unchanged_registry_counts={'sources':len(lock['upstreams']),'components':len(lock['components']),'external_dependencies':len(lock['external_dependencies'])}); s.write(out/'verification.json',verification)
    for name in ('verification.json','promotion.json','dist-manifest.json','proposed-upstreams.lock.json'): copy_file(out/name,N/E/name)
    for root in (R,N):
        target=root/'migration/upstreams.lock.json'; assert target.read_bytes()==oldlock; pending=target.with_name(target.name+'.i2cp-receive-pending'); assert not pending.exists()
        with pending.open('x') as stream: stream.write(proposed.read_text()); stream.flush(); os.fsync(stream.fileno())
        os.replace(pending,target); assert target.read_bytes()==proposed.read_bytes()
    print('PROMOTED_AND_RECORDED',SOURCE,promotion['manifest_sha256'],'fresh5/5/5, products8/5/5, private5, receive4 per reference/direction; olddistdeleted')

if __name__=='__main__': main()
