"""Compile the three changed/test TUs against the actual qualified Native graph."""
from pathlib import Path
import hashlib,json,shlex,subprocess
R=Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';D=Path(__file__).resolve().parent;B=R/'build/public-shared-quota-runtime-20261121/native-gcc'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
sources=[('fixture','tests/private_sdk_bootstrap_tests.cpp','tests/CMakeFiles/biglybtc-private-sdk-bootstrap-tests.dir/private_sdk_bootstrap_tests.cpp.o'),('profile','services/i2p_provider/profile_owner.cpp','services/CMakeFiles/biglybtc_i2p_provider_owner.dir/i2p_provider/profile_owner.cpp.o'),('service','app/owned_i2p_service.cpp','app/CMakeFiles/biglybtc_desktop_host.dir/owned_i2p_service.cpp.o')]
compile_commands={}
for name,source,target in sources:
 lines=subprocess.check_output(['ninja','-C',str(B),'-t','commands',target],text=True).splitlines()
 candidates=[line for line in lines if ' -c '+str(N/'preview/native'/source) in line]
 assert len(candidates)==1,(source,len(candidates));compile_commands[name]=shlex.split(candidates[0])
lines=subprocess.check_output(['ninja','-C',str(B),'-t','commands','biglybtc-private-sdk-bootstrap-tests'],text=True).splitlines();link=shlex.split(lines[-1]);assert link[:2]==[':','&&'] and link[-2:]==['&&',':'];link=link[2:-2]
tasks=[];checks=[];pins={}
for variant,compiler,flags in [('san','/usr/bin/g++-16',['-O1','-g1','-fsanitize=address,undefined','-fno-omit-frame-pointer']),('clang','/usr/bin/clang++-24',['--gcc-install-dir=/usr/lib/gcc/x86_64-linux-gnu/16'])]:
 out=D/variant;out.mkdir(exist_ok=True);objects=[]
 for name,source,target in sources:
  obj=out/(name+'.o');argv=compile_commands[name].copy();argv[0]=compiler;argv[argv.index('-o')+1]=str(obj)
  argv[argv.index('-MF')+1]=str(out/(name+'.d'))
  argv[1:1]=flags;objects.append(str(obj))
  tasks.append(dict(name='private-bootstrap-'+variant+'-'+name+'-compile',argv=['timeout','500','nice','-n','10',*argv],cwd=str(B)))
 argv=link.copy();argv[0]=compiler;at=argv.index('tests/CMakeFiles/biglybtc-private-sdk-bootstrap-tests.dir/private_sdk_bootstrap_tests.cpp.o');argv[at:at+1]=objects;argv[argv.index('-o')+1]=str(out/'bootstrap-tests');argv[1:1]=flags
 tasks.append(dict(name='private-bootstrap-'+variant+'-link',argv=['timeout','180','nice','-n','10',*argv],cwd=str(B)))
 for mode in ['direct','service','direct-pending','service-pending','held-gate','stale-config','ctor-failure','config-during-ctor','fenced-private-rate']:
  checks.append(dict(name='private-bootstrap-'+variant+'-'+mode,argv=['timeout','90',str(out/'bootstrap-tests'),mode],env={'LD_LIBRARY_PATH':str(R/'build/tag-rate-cross-owner-20261121/sdk-install/lib')+':/opt/Qt/6.12.0/gcc_64/lib','QT_QPA_PLATFORM':'offscreen','ASAN_OPTIONS':'detect_leaks=1:halt_on_error=1','UBSAN_OPTIONS':'halt_on_error=1:print_stacktrace=1'}))
for arg in link:
 if arg.endswith('.a'):
  p=Path(arg);p=p if p.is_absolute() else B/p;pins[str(p)]=sha(p)
(D/'qualified-native-archives.json').write_text(json.dumps(pins,indent=2)+'\n')
(D/'variant-builds.json').write_text(json.dumps(tasks,indent=2)+'\n');(D/'variant-checks.json').write_text(json.dumps(checks,indent=2)+'\n')
print('Prepared three actual TUs per scoped SAN/Clang lane;SDK and remaining Native archives retain Release scope')
