#!/usr/bin/env python3
"""Explicit owned Host/UI one-owner Public + I2P mixed acceptance; import is inert and execution is root-coordinated."""
import argparse, errno, hashlib, json, os, pathlib, re, shutil, signal, socket
import subprocess, sys, time, runpy, fcntl
from types import SimpleNamespace

H = pathlib.Path(__file__).resolve().parent
TARGET = 'biglybtc-mixed-network-host-transfer-probe'
JAR_SHA = 'b0866aa843a55d9290755c7a5cc3091cda765374544914afb4d3557ae99994b5'

def sha(path):
    h=hashlib.sha256()
    with pathlib.Path(path).open('rb') as stream:
        for block in iter(lambda:stream.read(1024*1024),b''):h.update(block)
    return h.hexdigest()
def load(path):return json.loads(pathlib.Path(path).read_bytes())
def write(path,value):pathlib.Path(path).write_text(json.dumps(value,indent=2)+'\n')
def cache(path):
    return {m[1]:m[2] for line in pathlib.Path(path).read_text().splitlines()
            if (m:=re.match(r'^([^#/][^:]*):[^=]+=(.*)$',line))}

def namespace_preflight(pins):
    proof=pins['namespace_preflight']
    for name,row in proof.items():assert sha(row['path'])==row['sha256'],name
    result=load(proof['result']['path']);inputs=load(proof['inputs']['path'])
    actual=load(proof['preflight']['path']);parent=load(proof['parent']['path'])
    assert result['status']=='PASS_FIVE_NAMESPACE_LOOPBACK_PREFLIGHT' and result['exit_code']==0
    assert result['no_fallback'] and result['parent_descriptors_unchanged'] and not result['functional_subjects_executed']
    assert result['inputs_sha256']==proof['inputs']['sha256'] and result['preflight_sha256']==proof['preflight']['sha256']
    assert result['logs_sha256']=={'stdout.log':proof['stdout']['sha256'],'stderr.log':proof['stderr']['sha256']}
    assert inputs['source_sha256']==proof['source']['sha256'] and inputs['bwrap_sha256']==pins['fence_tool']['sha256']
    assert actual['parent_sha256']==inputs['parent_sha256']==proof['parent']['sha256']
    assert load(proof['root_review']['path'])['manifest_sha256']==proof['manifest']['sha256']
    assert actual['status']=='PASS_FIVE_NAMESPACE_LOOPBACK_PREFLIGHT' and not actual['functional_subjects_executed']
    assert set(actual['namespaces'])=={'user','net','pid','ipc','uts'}
    for name,row in actual['namespaces'].items():
        assert row['parent']==parent['namespaces'][name]['identity'] and row['actual']!=row['parent']
    assert actual['capabilities']==dict.fromkeys(('CapInh','CapPrm','CapEff','CapBnd','CapAmb'),0)
    assert actual['no_new_privileges'] and actual['interfaces']==['lo'] and actual['loopback_up']
    return runpy.run_path(proof['source']['path'])

def fence(parent_namespace,parent_sha256):
    raw=pathlib.Path('/work/namespace-parent.json').read_bytes()
    assert hashlib.sha256(raw).hexdigest()==parent_sha256,'Actual parent snapshot changed'
    parent=__import__('json').loads(raw)
    assert parent['namespaces']['net']['link']==parent_namespace
    proof=load(H/'source-pins.json')['namespace_preflight']
    assert sha('/namespace-fence.py')==proof['source']['sha256']
    value=runpy.run_path('/namespace-fence.py')['fence'](parent)
    assert value['proc_pid1_namespace']==value['namespaces']['pid']['link']
    value.pop('functional_subjects_executed')
    value.update(parent_sha256=parent_sha256,scope='actual provider acceptance environment fence')
    return value

def inside(parent_namespace,java,parent_sha256):
    import resource
    resource.setrlimit(resource.RLIMIT_CORE,(0,0))
    resource.setrlimit(resource.RLIMIT_FSIZE,(32*1024*1024,32*1024*1024))
    work=pathlib.Path('/work');commands=[];subjects=[];logs=[]
    result={'status':'FAIL','scope':'real-isolated-router-one-owner-mixed-Public-I2P-Host',
            'cache_setup':'three actual accepted signature-verified Enc4 LS2 via public store',
            'discovery_or_remote_tunnels_qualified':False,'default_backend_activated':False,
            'late_tracker_reply_qualified':False}
    write(work/'fence-before.json',fence(parent_namespace,parent_sha256))
    def start(label,args,environment=None):
        log=(work/(label+'.log')).open('wb');logs.append(log)
        process=subprocess.Popen(args,stdin=subprocess.PIPE,stdout=log,stderr=subprocess.STDOUT,
                                 start_new_session=True,close_fds=True,env=environment)
        subjects.append(process);commands.append({'label':label,'argv':args,'pid':process.pid});return process
    def check_tracker_failure():
        failure=work/'tracker-failure.json'
        if failure.exists():
            assert failure.stat().st_size<=4096,'Tracker failure report exceeds bound'
            actual=load(failure)
            assert actual['status']=='FAIL_TRACKER_APPLICATION'
            raise AssertionError('Actual tracker application failed: '+actual['stage']+' / '+actual['exception_class']+' / '+actual['message'])
    def wait_file(leaf,seconds,processes):
        deadline=time.monotonic()+seconds
        check_tracker_failure()
        while not (work/leaf).exists():
            check_tracker_failure()
            assert all(p.poll() is None for p in processes),f'Subject exited before {leaf}'
            assert time.monotonic()<deadline,f'Deadline waiting for {leaf}'
            time.sleep(.025)
        check_tracker_failure();return load(work/leaf)
    def send(process,line):
        assert process.poll() is None
        process.stdin.write((line+'\n').encode('ascii'));process.stdin.flush()
    def join(process,seconds):
        deadline=time.monotonic()+seconds
        while process.poll() is None:
            check_tracker_failure();remaining=deadline-time.monotonic()
            assert remaining>0,'Subject join deadline exceeded'
            try:process.wait(timeout=min(.05,remaining))
            except subprocess.TimeoutExpired:pass
        check_tracker_failure();assert process.returncode==0,f'Subject exit {process.returncode}'
    try:
        with socket.socket() as reserve:reserve.bind(('127.0.0.1',0));port=reserve.getsockname()[1]
        assert port>=1024
        def java_command(mode):
            data='/work/router' if mode=='router' else '/work/peer-data'
            if mode!='router':pathlib.Path(data).mkdir(mode=0o700)
            return [java,'-Xms32m','-Xmx256m','-XX:ActiveProcessorCount=1',
                    '-Djava.awt.headless=true','-Djava.net.preferIPv4Stack=true',
                    '-Duser.home=/work/home','-Djava.io.tmpdir=/tmp',
                    '-Di2p.dir.base='+data,'-Di2p.dir.config='+data,
                    '-Dtime.disabled=true','-Djbigi.enable=false','-Djcpuid.enable=false',
                    '-cp','/classes:/reference.jar','RouterMixedTracker',mode,'/work',str(port),'0','0']
        router=start('router',java_command('router'));wait_file('router-ready.json',70,[router])
        deadline=time.monotonic()+5
        while True:
            try:
                with socket.create_connection(('127.0.0.1',port),timeout=.2):pass
                break
            except OSError:
                assert router.poll() is None and time.monotonic()<deadline,'I2CP TCP listener unavailable'
                time.sleep(.025)
        tracker=start('tracker',java_command('tracker'));wait_file('tracker-ready.json',45,[router,tracker])
        native_environment=dict(os.environ,LD_DEBUG='files',LD_DEBUG_OUTPUT='/work/native-loader')
        probe=start('native',['/probe',str(port),'/work'],native_environment);started=wait_file('native-started.json',30,[router,tracker,probe])
        assert started['status']=='ACTUAL_PUBLIC_CONTROL_AND_MIXED_HOST_STARTED' and started['active_hashes']==2 and started['saved_hashes']==3
        send(router,'warm-owned-cache');warm=wait_file('cache-precondition.json',40,[router,tracker,probe])
        assert warm['status']=='ACTUAL_THREE_SIGNED_LS2_CACHE_PRECONDITION'
        route_retry=wait_file('mixed-route-restarted.json',300,[router,tracker,probe])
        assert route_retry['status']=='ACTUAL_MIXED_RETRY_ACCEPTED_TCP_CONNECTED' and route_retry['leech_destination_sha256']==started['leech_destination_sha256']
        send(router,'rewarm-route-cache');route_warm=wait_file('mixed-route-cache-precondition.json',40,[router,tracker,probe])
        assert route_warm['status']=='ACTUAL_RETRIED_THREE_SIGNED_LS2_CACHE_PRECONDITION' and route_warm['leech_lease_sha256']!=warm['leech_lease_sha256']
        controls=wait_file('shared-controls-ready.json',700,[router,tracker,probe])
        assert controls['status']=='ACTUAL_CROSS_NETWORK_CONTROLS_COMPLETED'
        send(router,'check-native-shared')
        shared=wait_file('shared-session-precondition.json',12,[router,tracker,probe])
        assert shared['status']=='ACTUAL_SAME_TWO_NATIVE_IDENTITIES_REGISTERED' and shared['clients']==3
        restart=wait_file('leech-restart-ready.json',25,[router,tracker,probe])
        assert restart['status']=='ACTUAL_HOST_CLOSED_FOR_SAME_IDENTITY_RESTART'
        assert (restart['original_state_A'],restart['original_state_B'],restart['original_state_C'])==(75,70,70) and restart['B_paused'] and not restart['C_paused']
        send(router,'leech-closed');retired=wait_file('leech-retired.json',12,[router,tracker,probe])
        assert retired['status']=='ACTUAL_LEECH_CLIENT_RETIRED_SEED_PRESERVED' and retired['remaining_clients']==2
        renewed=wait_file('native-restarted.json',40,[router,tracker,probe])
        assert renewed['status']=='ACTUAL_SAME_PROFILE_RESTARTED' and renewed['leech_destination_sha256']==started['leech_destination_sha256']
        send(router,'rewarm-owned-cache');warm2=wait_file('cache-restart-precondition.json',40,[router,tracker,probe])
        assert warm2['status']=='ACTUAL_RESTARTED_THREE_SIGNED_LS2_CACHE_PRECONDITION'
        assert warm2['leech_lease_sha256']!=route_warm['leech_lease_sha256']
        # The probe owns one absolute1000s end. This outer wait never extends it.
        join(probe,1050);native=load(work/'native-result.json');assert native['status']=='PASS'
        assert native['verified_bytes']==4194304 and native['all_payload_inodes_preserved'] and native['joined_before_disk_read']
        for field in ('mixed_single_torrent_both_source_ranges_verified','independent_controls','shared_download_slot_handoff',
                      'original_networks_preserved','cumulative_library_traffic_preserved',
                      'saved_75_70_pause_restored','all_payload_inodes_preserved',
                      'joined_before_disk_read','Host_profile_lock_released','stopped_private_sentinel_untouched',
                      'one_mixed_library_row','actual_BT_wire_identity_prefix_and_private_tail','public_partial_source_unchanged'):
            assert native[field],field
        assert native['I2P_progress_without_Public_source'] and native['Public_progress_after_mixed_router_EOF']
        partition=native['source_checked_partition'];assert partition=={'piece_bytes':16384,'Public_first_piece':0,'Public_piece_count':64,'I2P_first_piece':64,'I2P_piece_count':64,'I2P_verified_bytes':1048576}
        assert not native['direct_peer_injection']
        assert native['seed_destination_sha256']==started['seed_destination_sha256'] and native['leech_destination_sha256']==started['leech_destination_sha256']
        i2p_hash=(work/'torrent.hashes').read_bytes();hashes=(work/'coexist.hashes').read_bytes()
        assert len(i2p_hash)==20 and len(hashes)==40 and hashes[:20]!=hashes[20:] and hashes[20:]==i2p_hash
        expected_hashes=[hashes[i:i+20].hex() for i in (0,20)]
        transfers=native['transfers'];assert len(transfers)==2 and [row['info_hash'].lower() for row in transfers]==expected_hashes
        assert [row['network'] for row in transfers]==['Public','Public+I2P']
        for index,row in enumerate(transfers):
            expected=bytes((n*31+17+index*23)%251 for n in range(2097152))
            assert row['verified_bytes']==2097152 and row['initial_downloaded']==(index+1)*1000
            assert row['saved_downloaded']>=2097152+row['initial_downloaded'] and row['payload_sha256']==hashlib.sha256(expected).hexdigest()
            name=('sample-0.bin' if index==0 else 'mixed.bundle').encode();pieces=b''.join(hashlib.sha1(expected[o:o+16384]).digest() for o in range(0,len(expected),16384))
            geometry=b'6:lengthi2097152e' if index==0 else b'5:filesld6:lengthi1048576e4:pathl9:first.bineed6:lengthi1048576e4:pathl10:second.bineee'
            info=b'd'+geometry+b'4:name'+str(len(name)).encode()+b':'+name+b'12:piece lengthi16384e6:pieces'+str(len(pieces)).encode()+b':'+pieces+b'e'
            assert row['info_hash'].lower()==hashlib.sha1(info).hexdigest()
        public=native['Public_tracker'];assert public['seed_announces']>0 and public['leech_announces']>0 and public['actual_peer_advertisements']>0 and public['replies']>0 and not public['failure']
        assert 0<public['registered_seed_port']<65536 and public['accepted']<=256 and public['source_left']==0
        mixed_public=native['Mixed_Public_tracker'];assert mixed_public['seed_announces']>0 and mixed_public['leech_announces']>0 and mixed_public['actual_peer_advertisements']>0 and mixed_public['replies']>0 and not mixed_public['failure']
        assert mixed_public['source_left']==1048576 and mixed_public['accepted']<=256 and mixed_public['registered_seed_port']==public['registered_seed_port']
        assert (work/'mixed-preview.png').read_bytes().startswith(b'\x89PNG\r\n\x1a\n')
        inputs=load(work/'inputs.json');loader=work/('native-loader.'+str(probe.pid))
        loaded=re.findall(r'calling init:\s*(\S+)',loader.read_text())
        exact=[str(pathlib.Path(name).resolve()) for name in loaded]
        assert inputs['provider_artifact'] in exact,'Exact approved provider did not initialize'
        qt_loaded=[name for name in exact if '/libQt6' in name]
        assert qt_loaded and all(name.startswith(inputs['qt_root']+'/lib/') for name in qt_loaded),'Non-private Qt image'
        for library in ('libQt6Core.so.6','libQt6Gui.so.6','libQt6Widgets.so.6'):
            assert str((pathlib.Path(inputs['qt_root'])/'lib'/library).resolve()) in exact,'Missing private Qt GUI/Test image'
        platform=(pathlib.Path(inputs['qt_root'])/'plugins/platforms/libqoffscreen.so').resolve()
        assert str(platform) in exact,'Actual offscreen platform plugin did not initialize'
        write(work/'native-loaded.json',{'status':'PASS','provider':inputs['provider_artifact'],
              'provider_sha256':sha(inputs['provider_artifact']),'private_qt':qt_loaded,
              'loader_log':loader.name,'loader_log_sha256':sha(loader)})
        send(router,'native-closed');wait_file('native-retired.json',12,[router,tracker])
        send(tracker,'stop');join(tracker,15);tracker_result=load(work/'tracker-result.json')
        assert tracker_result['status']=='PASS' and len(tracker_result['hashes'])==1
        assert tracker_result['hashes'][0]['info_hash']==i2p_hash.hex()
        for row in tracker_result['hashes']:
            assert row['seed_announces']>0 and row['leech_announces']>0 and row['seed_replies']>0 and row['leech_replies']>0 and row['source_left']==1048576
        assert tracker_result['external_i2cp'] and tracker_result['session_closed']
        send(router,'stop');join(router,20);router_result=load(work/'router-result.json')
        assert router_result['status']=='PASS' and router_result['remaining_clients']==0 and router_result['public_transports']==0
        write(work/'fence-after.json',fence(parent_namespace,parent_sha256))
        result.update(status='PASS_REAL_ISOLATED_ROUTER_SINGLE_TORRENT_PUBLIC_I2P_MIXED',port=port,native=native,
                      tracker=tracker_result,router=router_result)
    except BaseException as error:result['failure']=repr(error)
    finally:
        # Only these actual Popen handles; never enumerate another router/JVM.
        for process in reversed(subjects):
            if process.poll() is None:
                process.terminate()
                try:process.wait(timeout=10)
                except subprocess.TimeoutExpired:process.kill();process.wait(timeout=5)
        for process,row in zip(subjects,commands):row['exit_code']=process.returncode
        for log in logs:log.close()
        write(work/'commands.json',commands);write(work/'result.json',result)
    return 0 if result['status']=='PASS_REAL_ISOLATED_ROUTER_SINGLE_TORRENT_PUBLIC_I2P_MIXED' else 1

def git(root,*args):
    return subprocess.check_output(['/usr/bin/git','-C',str(root),*args],timeout=30).decode().strip()

def source_current(root,commit,tree):
    assert git(root,'rev-parse','HEAD')==commit,'Native HEAD differs from built source'
    assert git(root,'rev-parse',commit+':preview/native')==tree,'Native source tree association differs'
    assert not git(root,'status','--porcelain','--untracked-files=all','--','preview/native'),'Native source is dirty'

def main():
    parser=argparse.ArgumentParser()
    for name in ('build-receipt','probe','provider-record','provider-approval','provider-source','runtime-manifest','output'):
        parser.add_argument('--'+name,type=pathlib.Path,required=True)
    parser.add_argument('--source-commit',required=True)
    parser.add_argument('--probe-sha256',required=True)
    parser.add_argument('--provider-approval-sha256',required=True)
    parser.add_argument('--provider-source-sha256',required=True)
    parser.add_argument('--runtime-manifest-sha256',required=True)
    parser.add_argument('--java',type=pathlib.Path,default=pathlib.Path('/usr/bin/java'))
    parser.add_argument('--javac',type=pathlib.Path,default=pathlib.Path('/usr/bin/javac'))
    parser.add_argument('--acknowledge-warmed-local-cache-only',action='store_true',required=True)
    args=parser.parse_args()
    assert re.fullmatch('[0-9a-f]{40}',args.source_commit)
    assert re.fullmatch('[0-9a-f]{64}',args.probe_sha256)
    assert all(re.fullmatch('[0-9a-f]{64}',value) for value in (args.provider_source_sha256,args.provider_approval_sha256,args.runtime_manifest_sha256))
    assert args.output.is_absolute() and args.output.parent.resolve()==(H/'runs').resolve() and not args.output.exists()
    pins=load(H/'source-pins.json')
    for name in ('router_parent_source','modified_jar','fence_tool','tracker_source','probe_source','native_java_source_manifest'):
        row=pins[name];assert sha(row['path'])==row['sha256'],name
    support=namespace_preflight(pins)
    assert sha(args.provider_approval)==args.provider_approval_sha256
    approval=load(args.provider_approval);record=load(args.provider_record)
    assert approval['schema']=='BIGLYBTC_MIXED_I2P_ROLE_APPROVAL_V1' and approval['status']=='ROOT_APPROVED_MIXED_I2P_ROLES'
    assert sha(args.runtime_manifest)==args.runtime_manifest_sha256==approval['runtime_manifest_sha256']
    assert pathlib.Path(approval['runtime_manifest']).resolve()==args.runtime_manifest.resolve()
    assert record['schema']=='BIGLYBTC_MIXED_I2P_SOURCE_RECORD_V1' and record['status']=='BUILT_DIRECTLY_TESTED_NOT_APPROVED'
    assert sha(args.provider_source)==args.provider_source_sha256==approval['source_capsule_sha256']
    provider_capsule=load(args.provider_source);assert provider_capsule.get('source_paths') or provider_capsule.get('source_sha256') or provider_capsule.get('source_maps'),'Missing actual provider source map'
    assert approval['source_build_record_sha256']==sha(args.provider_record)
    artifact=pathlib.Path(record['artifact']).resolve()
    assert approval['artifact_sha256']==record['artifact_sha256']==sha(artifact)
    assert approval['artifact_bytes']==record['artifact_bytes']==artifact.stat().st_size
    sdk=pathlib.Path(record['install_root']).resolve()/'include'
    headers={str(p.relative_to(sdk)):sha(p) for p in sorted((sdk/'libtorrent').rglob('*')) if p.is_file()}
    assert 200<=len(headers)<=2048 and all(not (sdk/name).is_symlink() for name in headers)
    recorded={name[len('include/'):]:digest for name,digest in record['installed_sha256'].items() if name.startswith('include/libtorrent/')}
    assert headers==approval['headers_sha256']==recorded
    assert hashlib.sha256(json.dumps(headers,sort_keys=True,separators=(',',':')).encode()).hexdigest()==approval['sdk_tree_sha256']
    actual=load(args.build_receipt)
    assert actual['status']=='PASS' and actual['source_commit']==args.source_commit
    assert re.fullmatch('[0-9a-f]{40}',actual['source_tree'])
    assert actual['commands'],'Empty build/test command receipt'
    for command in actual['commands']:
        assert command['exit_code']==0 and isinstance(command['argv'],list) and command['argv']
        log=pathlib.Path(command.get('log') or actual['log']);assert log.is_absolute()
        assert sha(log)==command['log_sha256'],'Build/test log changed'
    native_build=pathlib.Path(actual['cache']).resolve();nc=cache(native_build/'CMakeCache.txt')
    native=pathlib.Path(nc['CMAKE_HOME_DIRECTORY']).resolve();source_root=native.parent.parent
    assert native==source_root/'preview/native'
    source_current(source_root,args.source_commit,actual['source_tree'])
    assert sha(native/'tests/mixed_network_host_transfer_probe.cpp')==pins['probe_source']['sha256']
    assert pathlib.Path(nc['BIGLYBTC_PRIVATE_I2P_OWNER_APPROVAL']).resolve()==args.provider_approval.resolve()
    assert nc['BIGLYBTC_PRIVATE_I2P_OWNER_APPROVAL_SHA256']==args.provider_approval_sha256
    assert nc['BIGLYBTC_PRIVATE_I2P_PROVIDER_OWNER']=='ON'
    assert nc['BIGLYBTC_PERSISTENT_MIXED_I2P_RUNTIME']=='ON'
    assert nc.get('BIGLYBTC_PERSISTENT_COEXISTENCE_RUNTIME','OFF')=='OFF'
    role_manifest=pathlib.Path(nc['BIGLYBTC_PERSISTENT_RUNTIME_MANIFEST']).resolve()
    role_approval=pathlib.Path(nc['BIGLYBTC_PERSISTENT_MIXED_I2P_APPROVAL']).resolve()
    assert role_manifest==args.runtime_manifest.resolve() and sha(role_manifest)==args.runtime_manifest_sha256
    assert role_approval==args.provider_approval.resolve() and sha(role_approval)==nc['BIGLYBTC_PERSISTENT_MIXED_I2P_APPROVAL_SHA256']==args.provider_approval_sha256
    # Reuse the actual adopted CLOSED verifier. Its root pins, full source/SDK
    # maps, actual control receipts and role declarations remain authoritative.
    verifier=native/'engine/runtime-binding/verify_runtime_manifest_mixed.py'
    verified=runpy.run_path(str(verifier))['inspect'](SimpleNamespace(
        manifest=role_manifest,approval=role_approval,approval_sha256=args.provider_approval_sha256,
        library=artifact,include_root=sdk))
    runtime,verified_record,verified_approval=verified
    assert verified_record==record and verified_approval==approval
    assert pathlib.Path(runtime['source_build_record']).resolve()==args.provider_record.resolve()
    assert runtime['source_capsule_sha256']==args.provider_source_sha256
    assert record['required_exports']==provider_capsule['required_exports'] and len(record['required_exports'])==39
    public_config=native_build/'engine/runtime-binding/persistent_runtime_config.hpp'
    public_text=public_config.read_text()
    for value in (args.runtime_manifest_sha256,args.provider_approval_sha256,approval['artifact_sha256'],
                  args.provider_source_sha256,sha(args.provider_record),approval['sdk_tree_sha256']):assert value in public_text
    for macro in ('BIGLYBTC_PERSISTENT_RUNTIME_BOUND 1','BIGLYBTC_PERSISTENT_MIXED_I2P_BOUND 1','BIGLYBTC_PERSISTENT_COEXISTENCE_BOUND 0'):
        assert '#define '+macro in public_text
    expected_sdk=pathlib.Path(record['install_root']).resolve()/'lib/cmake/LibtorrentRasterbar'
    assert pathlib.Path(nc['LibtorrentRasterbar_DIR']).resolve()==expected_sdk
    images=actual['images'];image=images[TARGET];probe=args.probe.resolve()
    assert probe==pathlib.Path(image['path']).resolve() and probe.name==TARGET
    assert args.probe_sha256==image['sha256']==sha(probe) and image['bytes']==probe.stat().st_size
    assert 'BiglyBTC' in images,'Host/UI application build is not in the receipt'
    for row in images.values():
        assert sha(row['path'])==row['sha256'] and pathlib.Path(row['path']).stat().st_size==row['bytes']
    generated=list(native_build.rglob('private_i2p_runtime_config.hpp'));assert len(generated)==1
    config=generated[0].read_text()
    for field in ('artifact_sha256','sdk_tree_sha256','runtime_manifest_sha256','source_build_record_sha256','review_sha256','source_capsule_sha256'):
        assert approval[field] in config,field
    assert args.provider_approval_sha256 in config and str(approval['artifact_bytes']) in config
    assert '#define BIGLYBTC_PRIVATE_MIXED_ROLE_BOUND 1' in config
    qt=pathlib.Path(nc['Qt6_DIR']).parents[2].resolve()
    qt_files={str(p.relative_to(qt)):sha(p) for p in (
        qt/'lib/libQt6Core.so.6',qt/'lib/libQt6Gui.so.6',qt/'lib/libQt6Widgets.so.6',
        qt/'lib/libQt6Test.so.6',qt/'plugins/platforms/libqoffscreen.so')}
    fonts={str(p):sha(p) for p in (pathlib.Path('/etc/fonts/fonts.conf'),
        pathlib.Path('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'))}
    jar=pathlib.Path(pins['modified_jar']['path']);assert sha(jar)==JAR_SHA
    (H/'runs').mkdir(exist_ok=True)
    assert shutil.disk_usage(args.output.parent).free>=5*1024**3,'Require at least5GiB free before acceptance'
    args.output.mkdir(mode=0o700);out=args.output
    for name in ('home','tmp','classes','runtime','xdg-config','xdg-cache'):(out/name).mkdir(mode=0o700)
    inputs={'source_commit':args.source_commit,'source_tree':actual['source_tree'],
        'build_receipt':str(args.build_receipt.resolve()),'build_receipt_sha256':sha(args.build_receipt),
        'probe':image,'images':images,'provider_record':str(args.provider_record.resolve()),
        'provider_record_sha256':sha(args.provider_record),'provider_source':str(args.provider_source.resolve()),'provider_source_sha256':args.provider_source_sha256,'provider_approval':str(args.provider_approval.resolve()),
        'provider_approval_sha256':args.provider_approval_sha256,
        'mixed_runtime_manifest':str(role_manifest),'mixed_runtime_manifest_sha256':sha(role_manifest),
        'mixed_role_approval_sha256':sha(role_approval),'mixed_verifier':str(verifier),'mixed_verifier_sha256':sha(verifier),
        'public_runtime_config_sha256':sha(public_config),
        'provider_sdk_headers_sha256':headers,'provider_sdk_tree_sha256':approval['sdk_tree_sha256'],
        'provider_artifact':str(artifact),'provider_artifact_sha256':sha(artifact),'provider_artifact_bytes':artifact.stat().st_size,
        'private_runtime_config_sha256':sha(generated[0]),'qt_root':str(qt),'qt_files_sha256':qt_files,'font_inputs_sha256':fonts,
        'java':str(args.java.resolve()),'java_sha256':sha(args.java.resolve()),
        'javac':str(args.javac.resolve()),'javac_sha256':sha(args.javac.resolve()),'jar_sha256':JAR_SHA,
        'namespace_preflight':pins['namespace_preflight'],
        'fixture_sources':{name:sha(H/name) for name in ('RouterMixedTracker.java','run.py','source-pins.json')}}
    write(out/'inputs.json',inputs)
    compile_command=['/usr/bin/nice','-n','5',str(args.javac.resolve()),'-J-Xmx128m','-J-XX:ActiveProcessorCount=1',
        '-J-Duser.home='+str(out/'home'),'-J-Djava.io.tmpdir='+str(out/'tmp'),
        '--release','17','-cp',str(jar),'-d',str(out/'classes'),pins['router_parent_source']['path'],str(H/'RouterMixedTracker.java')]
    env={k:v for k,v in os.environ.items() if k not in ('JAVA_TOOL_OPTIONS','JDK_JAVA_OPTIONS','_JAVA_OPTIONS','CLASSPATH','LD_PRELOAD','LD_DEBUG','LD_DEBUG_OUTPUT')}
    env.update(HOME=str(out/'home'),TMPDIR=str(out/'tmp'))
    write(out/'javac-command.json',compile_command)
    try:
        with (out/'javac.log').open('wb') as log:
            compiled=subprocess.run(compile_command,stdout=log,stderr=subprocess.STDOUT,env=env,timeout=90,check=False)
        write(out/'javac-result.json',{'exit_code':compiled.returncode})
        assert compiled.returncode==0,out/'javac.log'
        classes={str(p.relative_to(out/'classes')):sha(p) for p in (out/'classes').rglob('*.class')}
        assert set(classes)=={'RouterTracker.class','RouterTracker$Tracker.class','RouterMixedTracker.class','RouterMixedTracker$Counts.class','RouterMixedTracker$Request.class'},classes
        write(out/'classes.json',classes)
    except BaseException as error:
        write(out/'result.json',{'status':'FAIL_JAVA_FIXTURE_COMPILE','failure_class':type(error).__name__})
        for name in ('home','tmp','classes','runtime','xdg-config','xdg-cache'):shutil.rmtree(out/name)
        write(out/'private-cleanup.json',{'owned_private_fixture_paths_removed':True,'subject_keys_published':False})
        leaves=('inputs.json','javac-command.json','javac-result.json','javac.log','classes.json','result.json','private-cleanup.json')
        write(out/'public-artifacts.json',{str((out/name).resolve()):sha(out/name) for name in leaves if (out/name).is_file()})
        raise
    namespace_fds=[];parent_namespaces={};child=None
    try:
        for name,kind in support['NAMES'].items():
            fd=os.open('/proc/self/ns/'+name,os.O_RDONLY|os.O_CLOEXEC);namespace_fds.append(fd)
            assert fcntl.ioctl(fd,0xb703)==kind
            parent_namespaces[name]={'fd':fd,'identity':support['identity'](fd),'link':os.readlink('/proc/self/ns/'+name)}
        parent={'uid':os.getuid(),'gid':os.getgid(),'namespaces':parent_namespaces}
        write(out/'namespace-parent.json',parent)
        parent_sha=sha(out/'namespace-parent.json');inputs['namespace_parent_sha256']=parent_sha;write(out/'inputs.json',inputs)
        command=['/usr/bin/bwrap','--unshare-user','--unshare-net','--unshare-pid','--unshare-ipc','--unshare-uts',
            '--disable-userns','--cap-drop','ALL','--die-with-parent','--new-session','--clearenv',
            '--ro-bind','/usr','/usr','--ro-bind','/etc','/etc','--ro-bind','/lib','/lib']
        for path in ('/lib64','/opt'):
            if pathlib.Path(path).exists():command+=['--ro-bind',path,path]
        command+=['--proc','/proc','--dev','/dev','--tmpfs','/tmp','--tmpfs','/run','--dir','/home','--dir','/root',
            '--ro-bind',str(H),'/task','--ro-bind',pins['namespace_preflight']['source']['path'],'/namespace-fence.py',
            '--ro-bind',str(jar),'/reference.jar','--ro-bind',str((out/'classes').resolve()),'/classes',
            '--ro-bind',str(probe),'/probe','--ro-bind',str(artifact.parent),str(artifact.parent),
            '--ro-bind',str(qt),str(qt),'--bind',str(out.resolve()),'/work',
            '--ro-bind',str(out/'namespace-parent.json'),'/work/namespace-parent.json',
            '--setenv','PATH','/usr/bin:/bin','--setenv','HOME','/work/home','--setenv','LANG','C.UTF-8',
            '--setenv','TMPDIR','/tmp','--setenv','LD_LIBRARY_PATH',str(artifact.parent)+':'+str(qt/'lib'),
            '--setenv','QT_PLUGIN_PATH',str(qt/'plugins'),'--setenv','QT_QPA_PLATFORM','offscreen',
            '--setenv','QT_QPA_PLATFORM_PLUGIN_PATH',str(qt/'plugins/platforms'),
            '--setenv','XDG_RUNTIME_DIR','/work/runtime','--setenv','XDG_CONFIG_HOME','/work/xdg-config',
            '--setenv','XDG_CACHE_HOME','/work/xdg-cache','--chdir','/work',
            '/usr/bin/python3','/task/run.py','--inside',os.readlink('/proc/self/ns/net'),str(args.java.resolve()),parent_sha]
        write(out/'namespace-command.json',command)
        support['validate_parent'](parent);assert sha(out/'namespace-parent.json')==parent_sha
        with (out/'namespace.log').open('wb') as log:
            child=subprocess.Popen(command,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
            try:code=child.wait(timeout=1200)
            except subprocess.TimeoutExpired:os.killpg(child.pid,signal.SIGKILL);child.wait();code=124
        support['validate_parent'](parent);assert sha(out/'namespace-parent.json')==parent_sha
        write(out/'namespace-exit.json',{'exit_code':code,'parent_descriptors_unchanged':True})
        if not (out/'result.json').exists():write(out/'result.json',{'status':'FAIL_FENCE_OR_SETUP','namespace_exit_code':code})
        assert code==0 and load(out/'result.json')['status']=='PASS_REAL_ISOLATED_ROUTER_SINGLE_TORRENT_PUBLIC_I2P_MIXED',out/'namespace.log'
        assert sha(args.build_receipt)==inputs['build_receipt_sha256']
        assert sha(role_manifest)==inputs['mixed_runtime_manifest_sha256'] and sha(role_approval)==inputs['mixed_role_approval_sha256'] and sha(public_config)==inputs['public_runtime_config_sha256']
        assert sha(verifier)==inputs['mixed_verifier_sha256']
        assert sha(args.provider_record)==inputs['provider_record_sha256']
        assert sha(generated[0])==inputs['private_runtime_config_sha256']
        assert sha(args.provider_source)==args.provider_source_sha256 and sha(args.provider_approval)==args.provider_approval_sha256
        assert sha(probe)==image['sha256'] and sha(artifact)==approval['artifact_sha256'] and sha(jar)==JAR_SHA
        assert {str(p.relative_to(sdk)):sha(p) for p in sorted((sdk/'libtorrent').rglob('*')) if p.is_file()}==headers
        assert all(sha(qt/name)==digest for name,digest in qt_files.items())
        assert all(sha(path)==digest for path,digest in fonts.items())
        assert {str(p.relative_to(out/'classes')):sha(p) for p in (out/'classes').rglob('*.class')}==classes
        for row in images.values():assert sha(row['path'])==row['sha256']
        source_current(source_root,args.source_commit,actual['source_tree'])
        assert all(sha(H/name)==digest for name,digest in inputs['fixture_sources'].items()),'Fixture changed'
        assert runpy.run_path(str(verifier))['inspect'](SimpleNamespace(
            manifest=role_manifest,approval=role_approval,approval_sha256=args.provider_approval_sha256,
            library=artifact,include_root=sdk))==verified
        namespace_preflight(pins)
        write(out/'inputs-after.json',{'status':'UNCHANGED','source_commit':args.source_commit,'inputs_sha256':sha(out/'inputs.json')})
    finally:
        # Join the one namespace supervisor before touching its owned profiles,
        # including interruption or an unexpected exception in the outer wait.
        if child is not None and child.poll() is None:
            try:os.killpg(child.pid,signal.SIGTERM)
            except ProcessLookupError:pass
            try:child.wait(timeout=10)
            except subprocess.TimeoutExpired:
                try:os.killpg(child.pid,signal.SIGKILL)
                except ProcessLookupError:pass
                child.wait(timeout=5)
        for fd in namespace_fds:os.close(fd)
        # Fixed fresh owned paths only. No private profile/key is a public artifact.
        private=('router','peer-data','seed-profile','leech-profile','seed-payload','leech-payload','public-source-payload',
            'home','tmp','classes','runtime','xdg-config','xdg-cache')
        for name in private:
            path=out/name
            if path.exists():shutil.rmtree(path)
        for name in ('seed.keys','leech.keys'):(out/name).unlink(missing_ok=True)
        assert not any((out/name).exists() for name in private+('seed.keys','leech.keys'))
        write(out/'private-cleanup.json',{'owned_private_fixture_paths_removed':True,'subject_keys_published':False})
        public_names=('inputs.json','inputs-after.json','javac-command.json','javac-result.json','javac.log','classes.json',
            'namespace-parent.json','namespace-command.json','namespace-exit.json','namespace.log','commands.json','result.json',
            'fence-before.json','fence-after.json','native-loaded.json','native-started.json','native-result.json',
            'tracker-ready.json','cache-precondition.json','native-retired.json','tracker-result.json',
            'router-ready.json','router-result.json','seeder-announced.json','native.log','tracker.log','router.log',
            'native-phase.json','native-progress.json','native-terminal.json','seed.dest','leech.dest','tracker.dest',
            'tracker.url','torrent.hashes','coexist.hashes','mixed-preview.png','private-cleanup.json',
            'mixed-route-restarted.json','mixed-route-cache-precondition.json','tracker-progress.json','tracker-failure.json','tracker-eof.json','tracker-timeouts.json','shared-controls-ready.json','shared-session-precondition.json','leech-restart-ready.json',
            'leech-retired.json','native-restarted.json','cache-restart-precondition.json')
        public={str((out/name).resolve()):sha(out/name) for name in public_names if (out/name).is_file()}
        if (out/'commands.json').is_file():
            for row in load(out/'commands.json'):
                if row['label']=='native' and isinstance(row['pid'],int) and row['pid']>0:
                    loader=out/('native-loader.'+str(row['pid']))
                    if loader.is_file():public[str(loader.resolve())]=sha(loader)
        write(out/'public-artifacts.json',public)
    print(out/'result.json');return 0

if __name__=='__main__':
    if len(sys.argv)==5 and sys.argv[1]=='--inside':raise SystemExit(inside(sys.argv[2],sys.argv[3],sys.argv[4]))
    raise SystemExit(main())
