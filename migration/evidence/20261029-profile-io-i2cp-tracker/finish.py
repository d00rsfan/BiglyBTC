#!/usr/bin/env python3
"""One-shot closeout after actual gates; no build, test or Java replay."""
import ctypes, importlib.util, json, os, pathlib, re, shutil, subprocess
import xml.etree.ElementTree as ET
R=pathlib.Path('/home/yurii/Java/BiglyBT'); N=R.parent/'BiglyBTC'; H=pathlib.Path(__file__).resolve().parent
E=pathlib.Path('migration/evidence/20261029-profile-io-i2cp-tracker')
SOURCE='f02385070badd3318a250b2d8dd4146727964ac4'; ATTEMPT='r1'
ADOPTION_SHA='635b979f64fcc48bff82160187574fa833645f1503ac5032f944f4d3fc99c031'
OLD_DIST='125076ce7e38ca3f958aa46187235fdaa8b454202399cb9c474a8ed166237cee'
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
    assert adoption['source_commit']==SOURCE and adoption['source_path_count']==len(adoption['source_paths'])==31
    for name,row in adoption['source_paths'].items(): assert s.sha(N/name)==s.sha(row['after_path'])==row['after_sha256']
    snapshot=s.source_snapshot(SOURCE); lanes={}; images={}
    for lane in ('gcc','clang','san'):
        root=H/'runs'/(lane+'-'+ATTEMPT); d=s.load(root/'result.json'); suites=v.batch.suites_for_lane(lane)
        assert d['status']=='PASS_PROFILE_IO_I2CP_TRACKER_COHORT' and d['lane']==lane and d['source_commit']==SOURCE
        assert d['actual_suites']==len(suites) and d['source_unchanged'] and d['images_unchanged'] and not d['historical_passes_pooled']
        assert d['runner_sha256']==s.sha(H/'run.py') and d['source_receipt_sha256']==ADOPTION_SHA
        assert s.load(root/'source-before.json')==snapshot and s.sha(root/'tests.xml')==d['tests_xml_sha256'] and s.sha(root/'ctest-full.log')==d['full_ctest_log_sha256']
        assert s.passed_xml(root/'tests.xml',suites)==d['suite_names']
        assert all(row.get('status')=='run' for row in ET.parse(root/'tests.xml').getroot().iter('testcase'))
        assert not re.search(r'^(?:SKIP\s*:|FAIL!)',(root/'ctest-full.log').read_text(errors='replace'),re.M)
        commands(root,d,['build','tests']); images[lane]=s.load(root/'images-before.json')
        expected=set(suites.values()) | (set(v.batch.PRODUCTS) if lane=='gcc' else set()); assert set(images[lane])==expected
        for image in images[lane].values(): assert s.sha(image['path'])==image['sha256'] and pathlib.Path(image['path']).stat().st_size==image['bytes']
        lanes[lane]={'result_sha256':s.sha(root/'result.json'),'actual_suites':len(suites),'suite_names':d['suite_names'],'compiled_products':len(expected),'sanitizer_environment':d['sanitizer_environment']}
    privatepath=H/'private-runs'/ATTEMPT/'result.json'; private=s.load(privatepath)
    assert private['status']=='PASS_PRIVATE_PROFILE_IO_I2CP_TRACKER' and private['source_commit']==SOURCE and private['source_unchanged'] and private['runner_sha256']==s.sha(H/'private.py')
    assert not private['builds_executed'] and not private['provider_rebuilt'] and not private['historical_passes_pooled']
    assert set(private['lane_inputs'])==set(lanes) and [row['suite'] for row in private['actual_tests']]==list(v.batch.SUITES)
    assert set(private['copied_images'])==set(v.batch.SUITES.values())
    for lane,row in private['lane_inputs'].items(): assert row['sha256']==s.sha(H/'runs'/(lane+'-'+ATTEMPT)/'result.json')
    commands(privatepath.parent,private,['stage',*[name for suite in v.batch.SUITES for name in ('loader-'+suite,suite)]])
    offlinepath=H/'offline-runs'/ATTEMPT/'result.json'; offline=s.load(offlinepath)
    assert offline['status']=='PASS_ACTUAL_I2CP_TRACKER_OBJECTS' and offline['source_commit']==SOURCE and offline['source_unchanged'] and offline['native_images_unchanged']
    assert offline['runner_sha256']==s.sha(H/'offline.py') and offline['gcc_result_sha256']==s.sha(H/'runs'/('gcc-'+ATTEMPT)/'result.json')
    assert offline['cases_per_reference_per_direction']==2 and offline['message_frames_per_reference_per_direction']==10 and set(offline['class_origins'])=={'official','modified'} and not offline['old_java_replays']
    commands(offlinepath.parent,offline,['tracker-objects'])
    capsules=[*adoption['source_history'],*adoption['source_capsules']]
    for item in [*capsules,*adoption['source_reviews']]: assert s.sha(item['path'])==item['sha256']
    for item in capsules:
        cap=pathlib.Path(item['path']); m=s.load(cap)
        for name,pin in m.get('artifacts',m.get('artifacts_sha256',{})).items(): assert s.sha(cap.parent/name)==pin
        for ref in m.get('cmake_append_fragments',[]):
            if isinstance(ref,dict): assert s.sha(ref['path'])==ref['sha256']
    candidate=pathlib.Path(private['candidate']); assert not candidate.is_symlink() and candidate.resolve().parent==privatepath.parent.resolve()
    assert s.sha(candidate/'BUILD_MANIFEST.json')==private['candidate_manifest_sha256']; new=v.bundle(candidate); old=v.bundle(N/'dist')
    assert s.sha(N/'dist/BUILD_MANIFEST.json')==private['old_dist_unchanged']==OLD_DIST
    oldlock=(R/'migration/upstreams.lock.json').read_bytes(); assert (N/'migration/upstreams.lock.json').read_bytes()==oldlock
    lock=json.loads(oldlock); before=json.loads(oldlock); components={c['id']:c for c in lock['components']}
    groups={
      'transport.azureus_volatile_database':[p for p in adoption['source_paths'] if 'azureus' in p or '/azureus_udp/' in p],
      'services.i2cp_initial_session':[p for p in adoption['source_paths'] if '/i2cp' in p or p.endswith(('udp_tracker_wire.hpp','library_udp_tracker.hpp','services/CMakeLists.txt'))],
      'profile.config':['preview/native/adapters/profile.hpp','preview/native/adapters/profile.cpp'],
      'profile.torrent_state':['preview/native/adapters/torrent_state.hpp','preview/native/adapters/torrent_state.cpp','preview/native/tests/torrent_state_writer_guards.py']}
    for cid,paths in groups.items():
        c=components[cid]; append_unique(c['native_commits'],[{'repository':'https://github.com/d00rsfan/BiglyBTC.git','commit':SOURCE,'purpose':'integration'}])
        append_unique(c['review_records'],[(E/'REVIEW.md').as_posix()]); append_unique(c['evidence'],[(E/'verification.json').as_posix()]); append_unique(c['target_paths'],paths)
    append_unique(components['transport.azureus_volatile_database']['task_records'],['migration/tasks/P06-profile-io-executor.md'])
    c=components['services.i2cp_initial_session']; pins=s.load(R/'build/i2cp-tracker-scrape-20261029/source-pins.json'); coverage=next(x for x in c['source_coverage'] if x['upstream_id']=='i2p-java')
    append_unique(coverage['source_paths'],[x['path'] for x in pins['sources'] if 'jar_entry' in x])
    append_unique(coverage['source_symbols'],['HostLookupMessage','HostReplyMessage','SendMessageOptions.setSendLeaseSet','SendMessageExpiresMessage','I2PSession.lookupDest'])
    plugin={'upstream_id':'azneti2phelper','source_paths':[x['path'] for x in pins['sources'] if 'jar_entry' not in x],'source_symbols':['I2PSMHolder.lookupDest/lookupAddress','I2PHelperSocksProxy.UDPImpl','I2PHelperSocksProxy.SMHolderWrapper'],'baseline_commit':pins['plugin_commit'],'first_ported_commit':None,'integrated_through_commit':None,'last_reviewed_commit':pins['plugin_commit']}
    assert not any(x['upstream_id']=='azneti2phelper' for x in c['source_coverage']); c['source_coverage'].append(plugin)
    for b,a in zip(before['components'],lock['components'],strict=True):
        if b['id'] not in groups: assert b==a; continue
        assert b['state']==a['state'] and len(a['source_coverage'])>=len(b['source_coverage'])
        for bc,ac in zip(b['source_coverage'],a['source_coverage']):
            for key in ('upstream_id','baseline_commit','first_ported_commit','integrated_through_commit','last_reviewed_commit'): assert bc[key]==ac[key]
    assert before['upstreams']==lock['upstreams'] and before['external_dependencies']==lock['external_dependencies'] and len(lock['external_dependencies'])==22
    out=R/E; out.mkdir(parents=True); artifacts={}
    def copy(path,name):
        path=pathlib.Path(path); assert path.is_file() and not path.is_symlink() and path.stat().st_size<10*1024*1024
        with path.open('rb') as stream: assert stream.read(4)!=b'\x7fELF'
        target=out/name; copy_file(path,target); artifacts[name]=s.sha(target)
    for name in ('adopt.py','approved-inputs.json','root-adoption.json','run.py','private.py','offline.py','finish.py','recipe-source-review.json','native-tests.CMake.fragment'): copy(H/name,name)
    copy(N/'preview/native/tests/torrent_state_writer_guards.py','native-torrent-state-writer-guards.py')
    for lane in lanes:
        root=H/'runs'/(lane+'-'+ATTEMPT)
        for leaf in root.iterdir():
            if leaf.is_file() and leaf.suffix in ('.json','.log','.xml'): copy(leaf,'runs/'+root.name+'/'+leaf.name)
    for leaf in privatepath.parent.iterdir():
        if leaf.is_file() and leaf.suffix in ('.json','.log','.xml','.txt'): copy(leaf,'private/'+leaf.name)
    for leaf in offlinepath.parent.rglob('*'):
        if leaf.is_file() and leaf.suffix!='.class': copy(leaf,'offline/'+str(leaf.relative_to(offlinepath.parent)))
    for i,item in enumerate(capsules):
        cap=pathlib.Path(item['path']); copy(cap,'source/capsule-'+str(i)+'/manifest.json'); m=s.load(cap)
        for name in m.get('artifacts',m.get('artifacts_sha256',{})):
            if not name.startswith(('staging/','before/','original-source/','original-plugin/')): copy(cap.parent/name,'source/capsule-'+str(i)+'/'+name)
        for j,ref in enumerate(m.get('cmake_append_fragments',[])):
            if isinstance(ref,dict): copy(ref['path'],'source/capsule-'+str(i)+'/inherited-fragment-'+str(j)+'.cmake')
    for i,item in enumerate(adoption['source_reviews']): copy(item['path'],'source/review-'+str(i)+'.json')
    copy(N/'dist/BUILD_MANIFEST.json','previous-dist-manifest.json')
    verification={'status':'PASS_PROFILE_IO_I2CP_TRACKER','source_commit':SOURCE,'source_paths':31,'lanes':lanes,'private_commands':17,'actual_tracker_cases_per_reference_per_direction':2,'actual_tracker_frames_per_reference_per_direction':10,'provider_rebuilt':False,'provider_sha256':private['provider_sha256'],'historical_passes_pooled':False,'source_history':adoption['source_history'],'artifacts_sha256':artifacts,'promotion':None,'real_router_or_profile_used':False,'public_network_activated':False,'whole_feature_or_performance_parity_claimed':False}
    s.write(out/'verification.json',verification)
    (out/'REVIEW.md').write_text(f'''# Bounded profile I/O and explicit I2CP tracker

Source `{SOURCE}` changes31 exact paths. Fresh GCC17/Clang12/leak-enabled ASan+UBSan12 suites pass; GCC builds20 products including app/headless/smoke, other lanes12 affected products. Private runtime executes the same17 GCC commands using copied exact ELFs and measured private libraries. Its Python-wrapped state writer forwards actual child loader evidence and requires copied-ELF initialization. Seven native-only save/crash/conflict/foreign-profile/immutable controls reuse the deterministic existing fixture shape; no prior Java state oracle is replayed.

The existing Executor handles bounded profile reads/version reservations off the network actor. Eight I/O slots and64 frame holds, same-OFD recursive source gate before version mutex, one-shot current-revision packet permits, queued coalesced lifetime-bound mailbox delivery, cancellation/deadline and source withdrawal remain finite. Network continuations perform no filesystem operation, gate acquisition or future wait. Socket progress/close precedes external worker join. A syscall or already-entered source gate may still delay full join; no kernel-stall latency/performance claim. Blocked source parents ad6a/3d114 remain history, not qualified runtime origins; final8962 restricts pending-rating Cancel to live unconsumed/unexpired slots and retains the nested-observer correction. Existing profile.config/profile.torrent_state states and compatible write schemas remain unchanged; the DB component remains in_progress.

The explicit I2CP operation uses hash-only Host38/39, full Destination binding, actual owned signer/current session and LS2, signed2 BEP15 connect followed by claimed3 single-hash scrape. RAW18 replies remain unauthenticated confined observations, never general receiver/rank/payload authority. One deadline, fixed1024-operation/nonreused transaction history, no automatic retry/fallback and queued generation-fenced notifications bound lifecycle. The shared BEP15 body/old engine class are byte-exact, with mainline-single-udp regression. Each exact official/modified Java reference exchanges two signing families and10 total message frames in each direction with the actual current native codec; class origins, command/source/ELF hashes bind the observations. No old Java projections or provider rebuild/replay.

No real router, user profile, default/public/Host/Streaming/tunnel acceptance, complete active plugin or broader phase parity is claimed. Modified whole-core/source equivalence remains open. I2CP/volatile DB stay in_progress, both profile foundation states and all existing coverage baseline/first/integrated/null fields remain unchanged. Exact already-registered plugin33d97 and core9134 tracker-source coverage is additive. Both proposed registry preflights precede the atomic single-dist exchange. All actual current receipts/logs/XML and rejected source histories are retained without ELF/JAR/class/build-tree copies; no historical PASS pooling.
''')
    artifacts['REVIEW.md']=s.sha(out/'REVIEW.md'); shutil.copytree(out,N/E)
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
    promotion={'status':'PROMOTED_ONE_CHECKED_DEVELOPMENT_DIST','source_commit':SOURCE,'manifest_sha256':s.sha(N/'dist/BUILD_MANIFEST.json'),'leaf_count':len(new['files_sha256']),'previous_manifest_sha256':OLD_DIST,'previous_dist_removed':True,'atomic_exchange':True,'copied_test_images_removed':17,'test_support_removed':True,'both_registry_preflights_passed':True}
    s.write(H/'promotion.json',promotion); copy(H/'promotion.json','promotion.json'); copy(N/'dist/BUILD_MANIFEST.json','dist-manifest.json'); copy(proposed,'proposed-upstreams.lock.json')
    verification.update(promotion=promotion,registry_validation=validation,unchanged_registry_counts={'sources':len(lock['upstreams']),'components':len(lock['components']),'external_dependencies':len(lock['external_dependencies'])}); s.write(out/'verification.json',verification)
    for name in ('verification.json','promotion.json','dist-manifest.json','proposed-upstreams.lock.json'): copy_file(out/name,N/E/name)
    for root in (R,N):
        target=root/'migration/upstreams.lock.json'; assert target.read_bytes()==oldlock; pending=target.with_name(target.name+'.profile-io-i2cp-pending'); assert not pending.exists()
        with pending.open('x') as stream: stream.write(proposed.read_text()); stream.flush(); os.fsync(stream.fileno())
        os.replace(pending,target); assert target.read_bytes()==proposed.read_bytes()
    print('PROMOTED_AND_RECORDED',SOURCE,promotion['manifest_sha256'],'fresh17/12/12, products20/12/12, private17, tracker2families/10frames per reference/direction; olddistdeleted')

if __name__=='__main__': main()
