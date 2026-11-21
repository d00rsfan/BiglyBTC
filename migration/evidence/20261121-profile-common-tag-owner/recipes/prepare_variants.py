from pathlib import Path
import copy,json,re
D=Path(__file__).resolve().parent;R=D.parents[1];O=D/'native-overlay'
base=json.loads((D/'gcc-test-builds.json').read_text());service=copy.deepcopy(base)
for t in service:
 t['name']=t['name'].replace('common-tag-gcc-','common-tag-service-gcc-');a=t['argv'];a[:]=[x.replace(str(D/'common_owner_tests.cpp'),str(D/'service_owner_tests.cpp')).replace(str(D/'gcc/fixture.'),str(D/'gcc/service-fixture.')).replace(str(D/'gcc/common-owner-tests'),str(D/'gcc/service-owner-tests')) for x in a]
 if '-c' in a:a.insert(a.index('-c'),'-I'+str(O/'app'))
 else:
  assert 'app/libbiglybtc_desktop_host.a'in a
  a.insert(a.index('app/libbiglybtc_desktop_host.a'),str(D/'gcc/owned_i2p_service.o'))
(D/'gcc-final-builds.json').write_text(json.dumps(base+service,indent=2)+'\n')
checks=[]
for mode in ['close','no-checkpoint','destructor']:
 checks.append(dict(name='common-tag-gcc-owner-'+mode,argv=['timeout','90',str(D/'gcc/common-owner-tests'),mode],env=dict(LD_LIBRARY_PATH=str(R/'build/tag-rate-cross-owner-20261121/sdk-install/lib')+':/opt/Qt/6.12.0/gcc_64/lib',QT_QPA_PLATFORM='offscreen')))
for mode in ['tagged','foreign','default']:
 t=copy.deepcopy(checks[0]);t['name']='common-tag-gcc-service-'+mode;t['argv']=['timeout','90',str(D/'gcc/service-owner-tests'),mode];checks.append(t)
(D/'gcc-final-checks.json').write_text(json.dumps(checks,indent=2)+'\n')
for variant,compiler,flags in [('san','/usr/bin/g++-16',['-O1','-g1','-fsanitize=address,undefined','-fno-omit-frame-pointer']),('clang','/usr/bin/clang++-24',['--gcc-install-dir=/usr/lib/gcc/x86_64-linux-gnu/16'])]:
 (D/variant).mkdir(exist_ok=True);tasks=[]
 for old in json.loads((D/'gcc-builds.json').read_text())+base+service:
  t=copy.deepcopy(old);t['name']=t['name'].replace('-gcc-','-'+variant+'-');a=t['argv'];idx=next(i for i,x in enumerate(a) if Path(x).name in ['c++','g++-16']);a[idx]=compiler;a[:]=[x.replace(str(D/'gcc')+'/',str(D/variant)+'/') for x in a]
  if variant=='san':a[:]=[x for x in a if not re.fullmatch(r'-O[0-3sg]|-g[0-3]?',x)]
  a[idx+1:idx+1]=flags;tasks.append(t)
 (D/(variant+'-builds.json')).write_text(json.dumps(tasks,indent=2)+'\n');vs=[]
 for old in checks:
  t=copy.deepcopy(old);t['name']=t['name'].replace('-gcc-','-'+variant+'-');t['argv']=[x.replace(str(D/'gcc')+'/',str(D/variant)+'/') for x in t['argv']]
  if variant=='san':t['env'].update(ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
  vs.append(t)
 (D/(variant+'-checks.json')).write_text(json.dumps(vs,indent=2)+'\n')
print('Prepared serial GCC/scoped-SAN/Clang controller,engine,service and actual lifecycle tests')
