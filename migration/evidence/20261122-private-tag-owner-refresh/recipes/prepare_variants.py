from pathlib import Path
import copy,json,re,hashlib
D=Path(__file__).resolve().parent;base=[]
for name in ['gcc-builds.json','service-build.json','test-builds.json']:base+=json.loads((D/name).read_text())
for variant,compiler,flags in [('san','/usr/bin/g++-16',['-O1','-g1','-fsanitize=address,undefined','-fno-omit-frame-pointer']),('clang','/usr/bin/clang++-24',['--gcc-install-dir=/usr/lib/gcc/x86_64-linux-gnu/16'])]:
 (D/variant).mkdir(exist_ok=True);tasks=[]
 for old in base:
  t=copy.deepcopy(old);t['name']=t['name'].replace('-gcc-','-'+variant+'-');a=t['argv'];index=next(i for i,x in enumerate(a)if Path(x).name in ['c++','g++-16']);a[index]=compiler;a[:]=[x.replace(str(D/'gcc')+'/',str(D/variant)+'/')for x in a]
  if variant=='san':a[:]=[x for x in a if not re.fullmatch(r'-O[0-3sg]|-g[0-3]?',x)]
  a[index+1:index+1]=flags;tasks.append(t)
 (D/(variant+'-builds.json')).write_text(json.dumps(tasks,indent=2)+'\n');checks=copy.deepcopy(json.loads((D/'gcc-checks.json').read_text()))
 for t in checks:
  t['name']=t['name'].replace('-gcc-','-'+variant+'-');t['argv']=[x.replace(str(D/'gcc')+'/',str(D/variant)+'/')for x in t['argv']]
  if variant=='san':t['env'].update(ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
 (D/(variant+'-checks.json')).write_text(json.dumps(checks,indent=2)+'\n')
(D/'prototype-inputs.json').write_text(json.dumps({str(p.relative_to(D)):hashlib.sha256(p.read_bytes()).hexdigest()for p in [*sorted((D/'drafts').rglob('*.hpp')),*sorted((D/'drafts').rglob('*.cpp')),D/'owner_refresh_tests.cpp']},indent=2)+'\n')
print('Prepared scoped changed source/bridge/owner/service/fixture SAN and Clang,Release dependencies explicit')
