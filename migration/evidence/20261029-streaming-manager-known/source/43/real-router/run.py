#!/usr/bin/env python3
"""Root-run real external-manager Streaming acceptance; importing starts nothing."""
import argparse, errno, hashlib, json, os, pathlib, re, signal, socket
import subprocess, sys, time

H=pathlib.Path(__file__).resolve().parent
JAR_SHA='b0866aa843a55d9290755c7a5cc3091cda765374544914afb4d3557ae99994b5'
ARCHIVES=('biglybtc_i2p_streaming','biglybtc_i2cp_initial_channel','biglybtc_i2cp_tracker_codec',
          'biglybtc_i2cp_leaseset2','biglybtc_i2cp_datagram',
          'biglybtc_i2cp_session_config','biglybtc_i2p_client_identity',
          'biglybtc_i2p_datagram','biglybtc_i2cp_protocol')
SOURCE_DIRS=('i2p_streaming','i2cp_channel','i2cp_tracker','i2cp_datagram','i2cp_leaseset',
             'i2cp_session','i2p_client','i2p_datagram','i2cp')
def sha(p):
    h=hashlib.sha256()
    with pathlib.Path(p).open('rb') as f:
        for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
    return h.hexdigest()
def load(p):return json.loads(pathlib.Path(p).read_bytes())
def write(p,d):pathlib.Path(p).write_text(json.dumps(d,indent=2)+'\n')
def cache(p):
    return {m[1]:m[2] for line in pathlib.Path(p).read_text().splitlines()
            if (m:=re.match(r'^([^#/][^:]*):[^=]+=(.*)$',line))}
def fresh(p):
    p=pathlib.Path(p);assert p.is_absolute() and not p.exists(),p
    p.mkdir(mode=0o700);return p
def fence(parent_namespace):
    actual=os.readlink('/proc/self/ns/net');assert actual!=parent_namespace
    assert [name for _,name in socket.if_nameindex()]==['lo'],'Non-loopback interface'
    status=pathlib.Path('/proc/self/status').read_text()
    assert re.search(r'^CapEff:\s*0+$',status,re.M),'Capabilities retained'
    assert re.search(r'^NoNewPrivs:\s*1$',status,re.M),'NoNewPrivs absent'
    routes=pathlib.Path('/proc/net/route').read_text();assert not any(
        row.split()[0]!='lo' for row in routes.splitlines()[1:] if row.strip())
    if pathlib.Path('/proc/net/ipv6_route').exists():
        assert all(row.split()[-1]=='lo' for row in pathlib.Path('/proc/net/ipv6_route').read_text().splitlines() if row.strip())
    errors={}
    # These are unreachable documentation addresses in an already-verified
    # private namespace; no host/public route exists and no packets can leave.
    for family,address in ((socket.AF_INET,'192.0.2.1'),(socket.AF_INET6,'2001:db8::1')):
        for kind in (socket.SOCK_STREAM,socket.SOCK_DGRAM):
            try:
                with socket.socket(family,kind) as s:
                    s.settimeout(0.25);s.connect((address,9))
                raise AssertionError('Non-loopback route unexpectedly usable')
            except OSError as e:
                assert e.errno in (errno.ENETUNREACH,errno.EHOSTUNREACH,errno.EADDRNOTAVAIL,errno.EAFNOSUPPORT),e
                errors[f'{family}/{kind}']=e.errno
    return {'status':'ENFORCED_LOOPBACK_ONLY','network_namespace':actual,
            'host_namespace':parent_namespace,'interfaces':['lo'],
            'effective_capabilities':0,'no_new_privileges':True,
            'unreachable_controls_errno':errors}
def inside(parent_namespace,java):
    import resource
    resource.setrlimit(resource.RLIMIT_CORE,(0,0))
    resource.setrlimit(resource.RLIMIT_FSIZE,(32*1024*1024,32*1024*1024))
    work=pathlib.Path('/work');commands=[];subjects=[];logs=[]
    result={'status':'FAIL','scope':'real-isolated-router-original-manager-streaming',
            'cache_setup':'actual accepted+signature-verified LS2 via public store',
            'discovery_or_public_tunnels_qualified':False,'full_peer_destination_explicit':True}
    write(work/'fence-before.json',fence(parent_namespace))
    def start(label,args):
        log=(work/(label+'.log')).open('wb');logs.append(log)
        p=subprocess.Popen(args,stdin=subprocess.PIPE,stdout=log,stderr=subprocess.STDOUT,
                           start_new_session=True,close_fds=True)
        subjects.append(p);commands.append({'label':label,'argv':args,'pid':p.pid});return p
    def wait_file(leaf,seconds,processes):
        end=time.monotonic()+seconds
        while not (work/leaf).exists():
            assert all(p.poll() is None for p in processes),f'Subject exited before {leaf}'
            assert time.monotonic()<end,f'Deadline waiting for {leaf}'
            time.sleep(.025)
        return load(work/leaf)
    def send(p,line):assert p.poll() is None;p.stdin.write((line+'\n').encode('ascii'));p.stdin.flush()
    def join(p,seconds):p.wait(timeout=seconds);assert p.returncode==0,f'Subject exit {p.returncode}'
    try:
        with socket.socket() as reserve:
            reserve.bind(('127.0.0.1',0));port=reserve.getsockname()[1]
        assert port>=1024
        def java_command(mode):
            data='/work/router' if mode=='router' else '/work/peer-data'
            if mode!='router':pathlib.Path(data).mkdir(mode=0o700)
            return [java,'-Xms32m','-Xmx256m','-Djava.awt.headless=true',
                    '-Djava.net.preferIPv4Stack=true','-Duser.home=/work/home',
                    '-Djava.io.tmpdir=/tmp','-Di2p.dir.base='+data,
                    '-Di2p.dir.config='+data,'-Dtime.disabled=true',
                    '-Djbigi.enable=false','-Djcpuid.enable=false',
                    '-cp','/classes:/reference.jar','RouterStreaming',mode,'/work',str(port)]
        router=start('router',java_command('router'))
        wait_file('router-ready.json',70,[router])
        end=time.monotonic()+5
        while True:
            try:
                with socket.create_connection(('127.0.0.1',port),timeout=.2):pass
                break
            except OSError:
                assert router.poll() is None and time.monotonic()<end,'I2CP TCP listener unavailable'
                time.sleep(.025)
        tracker=start('tracker',java_command('tracker'))
        wait_file('tracker-ready.json',45,[router,tracker])
        probe=start('native',['/probe',str(port),'/work'])
        wait_file('native-lease.json',40,[router,tracker,probe])
        send(router,'warm-owned-cache')
        marker=wait_file('cache-precondition.json',35,[router,tracker,probe])
        assert marker['status']=='ACTUAL_SIGNED_LS2_CACHE_PRECONDITION'
        join(probe,80);assert load(work/'native-result.json')['status']=='PASS'
        send(router,'native-closed');wait_file('native-retired.json',12,[router,tracker])
        send(tracker,'stop');join(tracker,15);assert load(work/'tracker-result.json')['status']=='PASS'
        send(router,'stop');join(router,20);assert load(work/'router-result.json')['status']=='PASS'
        write(work/'fence-after.json',fence(parent_namespace))
        result.update(status='PASS_REAL_ISOLATED_ROUTER_STREAMING',
                      port=port,native=load(work/'native-result.json'),
                      tracker=load(work/'tracker-result.json'),router=load(work/'router-result.json'))
    except BaseException as e:result['failure']=repr(e)
    finally:
        # Only Popen objects created above. Never discover, signal, or inspect
        # another JVM/router. PID namespace destruction is the final boundary.
        for p in reversed(subjects):
            if p.poll() is None:
                p.terminate()
                try:p.wait(timeout=10)
                except subprocess.TimeoutExpired:p.kill();p.wait(timeout=5)
        for p,row in zip(subjects,commands):row['exit_code']=p.returncode
        for log in logs:log.close()
        (work/'native.keys').unlink(missing_ok=True)
        write(work/'commands.json',commands);write(work/'result.json',result)
    return 0 if result['status'].startswith('PASS_') else 1
def main():
    p=argparse.ArgumentParser();p.add_argument('--native-result',type=pathlib.Path,required=True)
    p.add_argument('--source-commit',required=True);p.add_argument('--probe-build',type=pathlib.Path,required=True)
    p.add_argument('--classes',type=pathlib.Path,required=True);p.add_argument('--jar',type=pathlib.Path,required=True)
    p.add_argument('--java',type=pathlib.Path,default=pathlib.Path('/usr/bin/java'))
    p.add_argument('--output',type=pathlib.Path,required=True)
    p.add_argument('--acknowledge-owned-signed-cache-precondition',action='store_true',required=True)
    a=p.parse_args();assert re.fullmatch('[0-9a-f]{40}',a.source_commit)
    assert a.output.parent.resolve()==(H/'runs').resolve(),'Output must be a fresh task-local run'
    assert sha(a.jar)==JAR_SHA and a.jar.is_file()
    pins=load(H/'source-pins.json');assert sha(pins['router_parent_source']['path'])==pins['router_parent_source']['sha256'];assert sha(pathlib.Path('/usr/bin/bwrap'))==pins['fence_tool']['sha256'],'Reviewed namespace tool changed'
    actual=load(a.native_result);assert actual['status']=='PASS_ROUTING_STREAMING_COHORT' and actual['lane']=='gcc' and actual['source_commit']==a.source_commit
    assert actual['source_unchanged'] and actual['images_unchanged'],'GCC qualification did not retain its source/images'
    snapshot=load(a.native_result.parent/'source-before.json');assert snapshot['source_commit']==a.source_commit
    native_build=pathlib.Path(actual['cache']).resolve();nc=cache(native_build/'CMakeCache.txt');native=pathlib.Path(nc['CMAKE_HOME_DIRECTORY'])
    pc=cache(a.probe_build/'CMakeCache.txt');assert pathlib.Path(pc['CMAKE_HOME_DIRECTORY']).resolve()==H
    assert pathlib.Path(pc['BIGLYBTC_NATIVE_SOURCE']).resolve()==native.resolve()
    assert pathlib.Path(pc['BIGLYBTC_NATIVE_BUILD']).resolve()==native_build
    assert pc['Qt6_DIR']==nc['Qt6_DIR'] and pathlib.Path(pc['CMAKE_CXX_COMPILER']).resolve()==pathlib.Path(nc['CMAKE_CXX_COMPILER']).resolve()
    source={}
    for directory in SOURCE_DIRS:
        for file in sorted((native/'services'/directory).glob('*')):
            if file.is_file() and file.suffix in ('.hpp','.cpp','.inc'):
                rel='preview/native/'+str(file.relative_to(native));assert sha(file)==snapshot['files_sha256'][rel],rel;source[rel]=sha(file)
    for rel in ['preview/native/core/include/biglybtc/udp_tracker_wire.hpp']:
        assert sha(native/rel.removeprefix('preview/native/'))==snapshot['files_sha256'][rel];source[rel]=snapshot['files_sha256'][rel]
    link_receipt=load(a.probe_build/'link-inputs.json')
    assert link_receipt['probe_source_sha256']==sha(H/'probe.cpp') and link_receipt['cmake_sha256']==sha(H/'CMakeLists.txt')
    links=link_receipt['archives']
    expected={str(native_build/'services'/('lib'+name+'.a')) for name in ARCHIVES};assert set(links)==expected
    assert all(sha(name)==value for name,value in links.items())
    probe=(a.probe_build/'biglybtc-i2p-real-router-streaming-probe').resolve();assert probe.is_file()
    classes={str(path.relative_to(a.classes)):sha(path) for path in a.classes.rglob('*.class')}
    assert set(classes)=={'RouterTracker.class','RouterTracker$Tracker.class','RouterStreaming.class'},'Unexpected classpath content'
    qt=pathlib.Path(nc['Qt6_DIR']).parents[2];assert (qt/'lib/libQt6Core.so.6').exists()
    (H/'runs').mkdir(exist_ok=True);out=fresh(a.output);(out/'home').mkdir(mode=0o700)
    inputs={'source_commit':a.source_commit,'native_result':str(a.native_result.resolve()),'native_result_sha256':sha(a.native_result),
            'native_source':source,'archives':links,'probe_sha256':sha(probe),'java':str(a.java.resolve()),'java_sha256':sha(a.java.resolve()),
            'jar_sha256':sha(a.jar),'fence_tool_sha256':sha(pathlib.Path('/usr/bin/bwrap')),
            'classes':classes,'fixture_sources':{name:sha(H/name) for name in ('RouterStreaming.java','probe.cpp','run.py','CMakeLists.txt','source-pins.json')}}
    write(out/'inputs.json',inputs)
    cmd=['/usr/bin/bwrap','--unshare-user','--unshare-net','--unshare-pid','--unshare-ipc','--unshare-uts',
         '--disable-userns','--cap-drop','ALL','--die-with-parent','--new-session','--clearenv',
         '--ro-bind','/usr','/usr','--ro-bind','/etc','/etc','--ro-bind','/lib','/lib']
    for path in ('/lib64','/opt'):
        if pathlib.Path(path).exists():cmd+=['--ro-bind',path,path]
    cmd+=['--proc','/proc','--dev','/dev','--tmpfs','/tmp','--tmpfs','/run',
          '--dir','/home','--dir','/root','--ro-bind',str(H),'/task',
          '--ro-bind',str(a.jar.resolve()),'/reference.jar','--ro-bind',str(a.classes.resolve()),'/classes',
          '--ro-bind',str(probe),'/probe','--bind',str(out.resolve()),'/work',
          '--setenv','PATH','/usr/bin:/bin','--setenv','HOME','/work/home','--setenv','LANG','C.UTF-8',
          '--setenv','TMPDIR','/tmp','--setenv','LD_LIBRARY_PATH',str(qt/'lib'),
          '--setenv','QT_PLUGIN_PATH',str(qt/'plugins'),'--chdir','/work',
          '/usr/bin/python3','/task/run.py','--inside',os.readlink('/proc/self/ns/net'),str(a.java.resolve())]
    write(out/'namespace-command.json',cmd)
    with (out/'namespace.log').open('wb') as log:
        child=subprocess.Popen(cmd,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
        try:rc=child.wait(timeout=330)
        except subprocess.TimeoutExpired:os.killpg(child.pid,signal.SIGKILL);child.wait();rc=124
    write(out/'namespace-exit.json',{'exit_code':rc})
    if not (out/'result.json').exists():
        write(out/'result.json',{'status':'FAIL_FENCE_OR_SETUP','namespace_exit_code':rc,
                                'reason':'No subject acceptance receipt; inspect namespace.log'})
    assert rc==0 and load(out/'result.json')['status']=='PASS_REAL_ISOLATED_ROUTER_STREAMING',out/'namespace.log'
    assert sha(probe)==inputs['probe_sha256'] and sha(a.jar)==JAR_SHA and all(sha(name)==value for name,value in links.items())
    assert {str(path.relative_to(a.classes)):sha(path) for path in a.classes.rglob('*.class')}==classes
    assert all(sha(native/rel.removeprefix('preview/native/'))==value for rel,value in source.items()),'Native source changed during acceptance'
    assert all(sha(H/name)==value for name,value in inputs['fixture_sources'].items()),'Fixture source changed during acceptance'
    assert sha(pins['router_parent_source']['path'])==pins['router_parent_source']['sha256']
    print(out/'result.json');return 0
if __name__=='__main__':
    if len(sys.argv)==4 and sys.argv[1]=='--inside':raise SystemExit(inside(sys.argv[2],sys.argv[3]))
    raise SystemExit(main())
