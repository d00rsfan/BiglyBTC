#!/usr/bin/env python3
"""One-shot accepted rating/datagram closeout; never builds or replays tests."""
import ctypes, importlib.util, json, os, pathlib, re, shutil, subprocess
import xml.etree.ElementTree as ET

R=pathlib.Path('/home/yurii/Java/BiglyBT'); N=R.parent/'BiglyBTC'; H=pathlib.Path(__file__).resolve().parent
E=pathlib.Path('migration/evidence/20261029-saved-ratings-i2cp-datagrams')
SOURCE='d384a850980f1b6245230ea852d617b11c405e11'
ADOPTION=H/'root-adoption-r2.json'; ADOPTION_SHA='22ac81bb31a005c92fbaeb826ca914bfd50eaa5db9b69008226b30e54dee2ebb'
OLD_DIST='92e3b83ba74ee2ec3b1886521914370a31074533daed7f2ed11d058804f2ab68'
spec=importlib.util.spec_from_file_location('private',H/'private-r2.py'); v=importlib.util.module_from_spec(spec); spec.loader.exec_module(v); s=v.s

def copy_file(source,target):
    target.parent.mkdir(parents=True,exist_ok=True); shutil.copyfile(source,target)

def commands(root,result,names):
    assert s.sha(root/'commands.json')==result['commands_sha256']
    rows=s.load(root/'commands.json'); assert [row['name'] for row in rows]==names
    for row in rows:
        assert row.get('exit')==0 and 'exception' not in row and s.sha(row['log'])==row['log_sha256']

def append_unique(values,items):
    for item in items:
        if item not in values: values.append(item)

def main():
    assert not (H/'promotion.json').exists() and not (R/E).exists() and not (N/E).exists()
    assert s.sha(ADOPTION)==ADOPTION_SHA
    adoption=s.load(ADOPTION); assert adoption['source_commit']==SOURCE and adoption['source_path_count']==21
    for name,row in adoption['source_paths'].items(): assert s.sha(N/name)==row['after_sha256']
    snapshot=s.source_snapshot(SOURCE); lanes={}; laneimages={}
    for lane in ('gcc','clang','san'):
        root=H/'runs'/(lane+'-r2'); d=s.load(root/'result.json'); suites=v.batch.suites_for_lane(lane)
        assert d['status']=='PASS_SAVED_RATING_DATAGRAM_COHORT' and d['source_commit']==SOURCE and d['lane']==lane
        assert d['actual_suites']==len(suites) and d['source_unchanged'] and d['images_unchanged'] and not d['historical_passes_pooled']
        assert d['runner_sha256']==s.sha(H/'run-r2.py') and d['source_receipt_sha256']==ADOPTION_SHA
        assert s.load(root/'source-before.json')==snapshot
        assert s.sha(root/'tests.xml')==d['tests_xml_sha256'] and s.sha(root/'ctest-full.log')==d['full_ctest_log_sha256']
        actual=s.passed_xml(root/'tests.xml',suites); assert actual==d['suite_names']
        assert all(row.get('status')=='run' for row in ET.parse(root/'tests.xml').getroot().iter('testcase'))
        assert not re.search(r'^(?:SKIP\s*:|FAIL!)',(root/'ctest-full.log').read_text(errors='replace'),re.M)
        commands(root,d,['build','tests']); laneimages[lane]=s.load(root/'images-before.json')
        expected=set(suites.values()) | (set(v.batch.PRODUCTS) if lane=='gcc' else set())
        assert set(laneimages[lane])==expected
        for image in laneimages[lane].values(): assert s.sha(image['path'])==image['sha256'] and pathlib.Path(image['path']).stat().st_size==image['bytes']
        lanes[lane]={'result_sha256':s.sha(root/'result.json'),'actual_suites':len(actual),'suite_names':actual,'compiled_products':len(expected),'sanitizer_environment':d['sanitizer_environment']}
    privatepath=H/'private-runs/r2/result.json'; private=s.load(privatepath)
    assert private['status']=='PASS_PRIVATE_SAVED_RATING_DATAGRAM' and private['source_commit']==SOURCE and private['source_unchanged']
    assert private['runner_sha256']==s.sha(H/'private-r2.py') and set(private['lane_inputs'])==set(lanes)
    assert [row['suite'] for row in private['actual_tests']]==list(v.batch.SUITES)
    for lane,row in private['lane_inputs'].items(): assert row['sha256']==s.sha(H/'runs'/(lane+'-r2/result.json'))
    commands(privatepath.parent,private,['stage',*[name for suite in v.batch.SUITES for name in ('loader-'+suite,suite)]])
    offlinepath=H/'offline-runs/r2-validation/result.json'; offline=s.load(offlinepath); raw_offline=H/'offline-runs/r2'
    assert offline['status']=='PASS_ACTUAL_DATAGRAM_SUBMISSION_OBJECTS' and offline['source_commit']==SOURCE
    assert offline['source_unchanged'] and offline['native_images_unchanged'] and not offline['old_java_replays']
    assert offline['runner_sha256']==s.sha(H/'offline-validate-r2.py') and offline['gcc_result_sha256']==s.sha(H/'runs/gcc-r2/result.json')
    failed_offline=s.load(raw_offline/'result.json'); assert failed_offline['status']=='FAIL' and failed_offline['failure']=="AssertionError(('official', 'native'))"
    assert failed_offline['runner_sha256']==s.sha(H/'offline-r2.py') and offline['original_failed_result_sha256']==s.sha(raw_offline/'result.json')
    assert offline['executions_replayed'] is False and pathlib.Path(offline['original_execution_directory'])==raw_offline
    assert s.sha(offlinepath.parent/'original-output-inventory.json')==offline['original_output_inventory_sha256']
    for name,pin in s.load(offlinepath.parent/'original-output-inventory.json').items(): assert s.sha(raw_offline/name)==pin
    assert (offlinepath.parent/'commands.json').read_bytes()==(raw_offline/'commands.json').read_bytes()
    assert (offline['send_cases_per_reference_per_direction'],offline['status_cases_per_reference_per_direction'],offline['payload_cases_per_reference_per_direction'])==(8,4,1)
    assert set(offline['class_origins'])=={'official','modified'}
    commands(offlinepath.parent,offline,['datagram-objects'])
    history=s.load(adoption['failed_r1_history']['path']); assert s.sha(adoption['failed_r1_history']['path'])==adoption['failed_r1_history']['sha256']
    assert history['status']=='ACTUAL_FAILED_R1_NO_FINAL_PASS_CREDIT'
    for rows in history['artifacts_sha256'].values():
        for name,pin in rows.items(): assert s.sha(H/name)==pin
    for item in [*adoption['source_capsules'],*adoption['source_reviews'],*adoption['correction_capsules'],*adoption['correction_reviews'],adoption['initial_adoption']]: assert s.sha(item['path'])==item['sha256']
    candidate=pathlib.Path(private['candidate']); assert candidate.resolve().parent==privatepath.parent.resolve() and not candidate.is_symlink()
    assert s.sha(candidate/'BUILD_MANIFEST.json')==private['candidate_manifest_sha256']; new=v.bundle(candidate); old=v.bundle(N/'dist')
    assert s.sha(N/'dist/BUILD_MANIFEST.json')==private['old_dist_unchanged']==OLD_DIST
    oldmanifest=(N/'dist/BUILD_MANIFEST.json').read_bytes(); lockpath=R/'migration/upstreams.lock.json'; oldlock=lockpath.read_bytes()
    oldlocks={root:(root/'migration/upstreams.lock.json').read_bytes() for root in (R,N)}; alignment=s.load(H/'registry-alignment/proof.json')
    assert alignment['reconstructed_N_plus_J_equals_R'] and s.sha(R/'migration/upstreams.lock.json')==alignment['R_before_sha256'] and s.sha(N/'migration/upstreams.lock.json')==alignment['N_before_sha256']
    lock=json.loads(oldlock); lockbefore=json.loads(oldlock)
    changed={'services.i2cp_initial_session','transport.azureus_volatile_database','plugins.azrating_local_policy'}
    components={c['id']:c for c in lock['components']}; assert changed<=components.keys()
    for cid in changed:
        c=components[cid]; append_unique(c['native_commits'],[{'repository':'https://github.com/d00rsfan/BiglyBTC.git','commit':SOURCE,'purpose':'integration'}])
        append_unique(c['review_records'],[(E/'REVIEW.md').as_posix()]); append_unique(c['evidence'],[(E/'verification.json').as_posix()])
    db=components['transport.azureus_volatile_database']; session=components['services.i2cp_initial_session']
    append_unique(db['target_paths'],[name for name in adoption['source_paths'] if 'azureus' in name or '/adapters/local_rating.' in name])
    append_unique(session['target_paths'],[name for name in adoption['source_paths'] if 'i2cp_' in name or 'i2cp/' in name])
    core=next(c for c in db['source_coverage'] if c['upstream_id']=='BiglyBT')
    append_unique(core['source_paths'],['core/src/com/biglybt/pifimpl/local/ddb/'+name+'.java' for name in ('DDBaseImpl','DDBaseKeyImpl','DDBaseHelpers')]+['core/src/com/biglybt/core/dht/DHT.java'])
    append_unique(core['source_symbols'],['DDBaseImpl.read','DDBaseImpl.write','DDBaseKeyImpl','DDBaseHelpers.encode','DHT.FLAG_ANON'])
    assert not any(c['upstream_id']=='azrating' for c in db['source_coverage'])
    db['source_coverage'].append({'upstream_id':'azrating','source_paths':['com/aelitis/azureus/plugins/rating/updater/RatingsUpdater.java','com/aelitis/azureus/plugins/rating/RatingPlugin.java'],'source_symbols':['RatingsUpdater.readRating(Download,CompletionListener)','RatingsUpdater.writeRating','RatingPlugin.getDDBs'],'baseline_commit':'1629340340d0bc391d9e582f9ee989a93d550581','first_ported_commit':None,'integrated_through_commit':None,'last_reviewed_commit':'1629340340d0bc391d9e582f9ee989a93d550581'})
    coverage=next(c for c in session['source_coverage'] if c['upstream_id']=='i2p-java')
    datapins=s.load(R/'build/i2cp-datagram-submit-20261029/source-pins.json')
    append_unique(coverage['source_paths'],[row['path'] for row in datapins['sources']])
    append_unique(coverage['source_symbols'],['SendMessageMessage','SendMessageExpiresMessage','MessageStatusMessage','MessagePayloadMessage','Datagram2.make/load','Datagram3.make/load','DataHelper.compress/decompress','ClientConnectionRunner.destinationEstablished'])
    for before,after in zip(lockbefore['components'],lock['components'],strict=True):
        assert before['id']==after['id'] and before['state']==after['state']
        for index,b in enumerate(before['source_coverage']):
            a=after['source_coverage'][index]
            for key in ('upstream_id','baseline_commit','first_ported_commit','integrated_through_commit','last_reviewed_commit'): assert b[key]==a[key]
        if before['id'] not in changed: assert before==after
    assert components['plugins.azrating_local_policy']['source_coverage']==next(c for c in lockbefore['components'] if c['id']=='plugins.azrating_local_policy')['source_coverage']
    assert lockbefore['upstreams']==lock['upstreams'] and lockbefore['external_dependencies']==lock['external_dependencies']
    out=R/E; out.mkdir(parents=True); artifacts={}
    def copy(path,name):
        path=pathlib.Path(path); assert path.is_file() and not path.is_symlink() and path.stat().st_size<10*1024*1024
        with path.open('rb') as stream: assert stream.read(4)!=b'\x7fELF'
        dest=out/name; copy_file(path,dest); artifacts[name]=s.sha(dest)
    for name in ('run.py','run-r2.py','private.py','private-r2.py','offline.py','offline-r2.py','offline-validate-r2.py','finish.py','finish-r2.py','finish-initial-failure.log','finish-source-review.json','adopt.py','adopt-r2.py','root-adoption.json','root-adoption-r2.json','approved-inputs.json','failed-r1-history.json','i2cp-source-review.json','recipe-source-review.json','offline-source-review.json','datagram-bounds-source-review.json','multivalue-readback-source-review.json','metadata-mapping-notes.txt'): copy(H/name,name)
    for leaf in (H/'registry-alignment').iterdir(): copy(leaf,'registry-alignment/'+leaf.name)
    for attempt in ('r1','r2'):
        for lane in lanes:
            for leaf in (H/'runs'/(lane+'-'+attempt)).iterdir():
                if leaf.is_file() and leaf.suffix in ('.json','.log','.xml'): copy(leaf,'runs/'+lane+'-'+attempt+'/'+leaf.name)
    for leaf in privatepath.parent.iterdir():
        if leaf.is_file() and leaf.suffix in ('.json','.log','.xml','.txt'): copy(leaf,'private/'+leaf.name)
    for leaf in raw_offline.rglob('*'):
        if leaf.is_file() and leaf.suffix!='.class': copy(leaf,'offline/original-r2/'+str(leaf.relative_to(raw_offline)))
    for leaf in offlinepath.parent.iterdir():
        if leaf.is_file(): copy(leaf,'offline/validation/'+leaf.name)
    def capsule(item,label):
        path=pathlib.Path(item['path']); assert s.sha(path)==item['sha256']; copy(path,'source/'+label+'/manifest.json'); m=s.load(path)
        for name,pin in m.get('artifacts',m.get('artifacts_sha256',{})).items():
            assert s.sha(path.parent/name)==pin
            if not name.startswith(('staging/','before/','original-source/')): copy(path.parent/name,'source/'+label+'/'+name)
        if 'parent_manifest' in m:
            ref=m['parent_manifest']; parent=ref if isinstance(ref,dict) else {'path':ref,'sha256':m['parent_manifest_sha256']}; capsule(parent,label+'/parent')
    for i,item in enumerate([*adoption['source_capsules'],*adoption['correction_capsules']]): capsule(item,'capsule-'+str(i))
    for i,item in enumerate([*adoption['source_reviews'],*adoption['correction_reviews']]):
        copy(item['path'],'source/reviews/'+str(i)+'-'+pathlib.Path(item['path']).name)
        review=s.load(item['path'])
        for key in ('review','history'):
            if isinstance(review.get(key),dict) and 'path' in review[key]:
                ref=review[key]; assert s.sha(ref['path'])==ref['sha256']; copy(ref['path'],'source/reviews/'+str(i)+'-'+key+'.json')
    copy(N/'dist/BUILD_MANIFEST.json','previous-dist-manifest.json')
    verification={'status':'PASS_BOUNDED_SAVED_RATINGS_AND_I2CP_DATAGRAM_OBJECTS','source_commit':SOURCE,'source_paths':21,'lanes':lanes,'private_commands':7,'actual_send_cases_per_reference_per_direction':8,'actual_status_cases_per_reference_per_direction':4,'actual_payload_cases_per_reference_per_direction':1,'provider_rebuilt':False,'provider_sha256':private['provider_sha256'],'historical_passes_pooled':False,'failed_r1_retained':True,'offline_original_failure_retained':True,'offline_validation_replayed_commands':False,'artifacts_sha256':artifacts,'promotion':None,'real_profile_used':False,'public_network_activated':False,'whole_feature_or_performance_parity_claimed':False}
    s.write(out/'verification.json',verification)
    (out/'REVIEW.md').write_text(f'''# Explicit saved ratings and lease-backed I2P datagrams

Qualified source `{SOURCE}` changes21 exact paths. Fresh GCC7/Clang5/SAN5 suites passed with10/5/5 measured products. Only GCC builds the app/headless/smoke products and unchanged local-rating UI/wiring regressions. Private relocation executes the seven exact GCC suite commands. Both pinned Java artifacts cross eight Send5/Expires36 signer/protocol/gzip cases, four Status22 and one Payload31 DTO in each direction, with actual class origins and exact current GCC ELF binding. No old Java projection or failed-attempt PASS pooling. Promotion and pre-promotion registry validation are recorded separately in verification.json.

Saved ratings use an explicit already-admitted numeric peer, same held profile/version OFD, full saved target identity, canonical Public and original DHT availability. Existing key/encoding/local-policy semantics are reused; ANON0x10 is a compatibility bit, not anonymous routing. Publication verifies its exact value across a bounded FINAL readback stream. Reads are one-peer/possiblyPartial,64 versus original256, not a global average. Empty remote VALUE_READ alone permits local fallback; malformed nonempty values do not. Source/cancel/close fences withdraw current values. No automatic publication queue, timers, plugin/Host/public bootstrap or whole-rating feature completion. Fresh profile I/O and version fsync remain synchronous performance debt.

Explicit Datagram2/claimed Datagram3 submission requires current retained signer, exact accepted Destination/session and actual successful LS2 enqueue with future expiry. CREATED alone grants nothing. One pending tracked nonce and one latest observation remain bounded; known router status certainty stays distinct from local timeout/cancel and end-to-end delivery. Independent local deadline survives lease/reconfigure events. Pure Send5/Expires36/Status22/Payload31 and fixed-header single-member gzip are bounded; Payload31 parsing grants no receive listener. No real router, tunnels, delivery, resolver, streaming or backend/default activation is qualified. Existing exact zlib is reused; no new dependency/provider build.

R1 remains failed: GCC fixture array-bounds compilation; Clang/SAN seven owned-rating failures. R2 corrects exactly three paths: checked gzip test mutation, multi-value publication continuation handling, and two strengthened/corrected owner fixture methods. The earlier source review missed recursive parser duplicate_key rejection; six ordinary-reader positive expectations were wrong, not evidence of a reachable inventory ambiguity. Both readers retain refusal. The explicit distributed row checks are defensive. Prior raw failures and source-review correction are retained; fresh final suites provide qualification.

The actual offline r2 command exited0 after every native/Java object comparison, but its wrapper report failed because the ninth status/payload summary shared the send-row prefix. The initial source review missed this prefix collision. A separate validator accepted exact eight send rows plus one summary and rechecked original outputs, source/ELFs/JAR class origins and command hashes without replaying any command. Raw failed result and byte-exact executed outputs remain separate from the accepted validation receipt.

Initial closeout stopped before mutation because the previous accepted milestone's native registry lacked its exact two-component metadata delta. The retained proof reconstructs the accepted R registry from the old N registry plus that exact prior delta; current proposed registries align both while preserving baseline fields. No source/build/test replay.\n\nThree existing registry records gain scoped integration/evidence. Volatile database and I2CP remain in_progress; local-policy implemented scope stays local load/store. Existing states, baseline/null coverage fields, upstreams and dependency records are preserved. No full public/automatic plugin or P06/P07 parity claim.
''')
    shutil.copytree(out,N/E)
    # Validate the exact proposed bytes using BOTH existing validators before
    # touching the checked dist or either published lock. Missing Git objects
    # therefore stop here with retained logs and the old dist/locks intact.
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
    for images in laneimages.values():
        for image in images.values(): assert s.sha(image['path'])==image['sha256']
    assert all((root/'migration/upstreams.lock.json').read_bytes()==oldlocks[root] for root in (R,N))
    libc=ctypes.CDLL(None,use_errno=True); exchange=libc.renameat2; exchange.argtypes=[ctypes.c_int,ctypes.c_char_p,ctypes.c_int,ctypes.c_char_p,ctypes.c_uint]
    assert exchange(-100,os.fsencode(N/'dist'),-100,os.fsencode(candidate),2)==0,os.strerror(ctypes.get_errno())
    assert v.bundle(N/'dist')==new and v.bundle(candidate)==old; shutil.rmtree(candidate)
    for target,image in private['copied_images'].items():
        copied=privatepath.parent/target; assert s.sha(copied)==image['sha256']; copied.unlink()
    shutil.rmtree(privatepath.parent/'test-support')
    promotion={'status':'PROMOTED_ONE_CHECKED_DEVELOPMENT_DIST','source_commit':SOURCE,'manifest_sha256':s.sha(N/'dist/BUILD_MANIFEST.json'),'leaf_count':len(new['files_sha256']),'previous_manifest_sha256':OLD_DIST,'previous_dist_removed':True,'atomic_exchange':True,'copied_test_images_removed':7,'test_support_removed':True,'both_registry_preflights_passed':True}
    s.write(H/'promotion.json',promotion); copy(H/'promotion.json','promotion.json'); copy(N/'dist/BUILD_MANIFEST.json','dist-manifest.json'); copy(proposed,'proposed-upstreams.lock.json')
    verification.update(promotion=promotion,registry_alignment=alignment,registry_validation=validation,unchanged_registry_counts={'sources':len(lock['upstreams']),'components':len(lock['components']),'external_dependencies':len(lock['external_dependencies'])})
    s.write(out/'verification.json',verification)
    for name in ('verification.json','promotion.json','dist-manifest.json','proposed-upstreams.lock.json'): copy_file(out/name,N/E/name)
    for root in (R,N):
        target=root/'migration/upstreams.lock.json'; assert target.read_bytes()==oldlocks[root]; pending=target.with_name(target.name+'.rating-datagram-pending'); assert not pending.exists()
        with pending.open('x') as stream: stream.write(proposed.read_text()); stream.flush(); os.fsync(stream.fileno())
        os.replace(pending,target); assert target.read_bytes()==proposed.read_bytes()
    print('PROMOTED_AND_RECORDED',SOURCE,promotion['manifest_sha256'],'fresh7/5/5, products10/5/5, private7, datagram8+4+1 per reference/direction; olddistdeleted')

if __name__=='__main__': main()
