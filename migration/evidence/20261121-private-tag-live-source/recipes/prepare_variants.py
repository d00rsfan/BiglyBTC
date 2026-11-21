from pathlib import Path
import copy,json,re,hashlib
D=Path(__file__).resolve().parent;prior=D.parent/'private-tag-source-20261121';base=json.loads((D/'gcc-builds.json').read_text());checks=json.loads((D/'gcc-checks.json').read_text())
for variant,compiler,flags in [('san','/usr/bin/g++-16',['-O1','-g1','-fsanitize=address,undefined','-fno-omit-frame-pointer']),('clang','/usr/bin/clang++-24',['--gcc-install-dir=/usr/lib/gcc/x86_64-linux-gnu/16'])]:
 (D/variant).mkdir(exist_ok=True);tasks=[]
 for old in base:
  t=copy.deepcopy(old);t['name']=t['name'].replace('-gcc-','-'+variant+'-')+'-final';a=t['argv'];idx=next(i for i,x in enumerate(a)if Path(x).name in ['c++','g++-16']);a[idx]=compiler;a[:]=[x.replace(str(D/'gcc')+'/',str(D/variant)+'/').replace(str(prior/'probe-gcc/libprofile-cohort.a'),str(prior/('probe-'+variant)/'libprofile-cohort.a'))for x in a]
  # The probe archive is relative to its actual variant cwd in link commands.
  t['cwd']=t['cwd'].replace('/private-tag-source-20261121/probe-gcc','/private-tag-source-20261121/probe-'+variant)
  if variant=='san':a[:]=[x for x in a if not re.fullmatch(r'-O[0-3sg]|-g[0-3]?',x)]
  a[idx+1:idx+1]=flags;tasks.append(t)
 (D/(variant+'-builds.json')).write_text(json.dumps(tasks,indent=2)+'\n');ts=[]
 for old in checks:
  t=copy.deepcopy(old);t['name']=t['name'].replace('-gcc-','-'+variant+'-')+'-final';t['argv']=[x.replace(str(D/'gcc')+'/',str(D/variant)+'/')for x in t['argv']]
  if variant=='san':t['env'].update(ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
  ts.append(t)
 (D/(variant+'-checks.json')).write_text(json.dumps(ts,indent=2)+'\n')
# Preserve already-run GCC12 logs;compile the final ABI1-branch fixture anew.
ts=copy.deepcopy(base[1:])
for t in ts:t['name']+='-final'
(D/'gcc-final-builds.json').write_text(json.dumps(ts,indent=2)+'\n')
for t in checks:t['name']+='-final'
(D/'gcc-final-checks.json').write_text(json.dumps(checks,indent=2)+'\n')
files=[D/'native-overlay/engine/profile_tag_rates.cpp',D/'native-overlay/engine/profile_tag_rates.hpp',D/'live_source_tests.cpp'];(D/'prototype-inputs.json').write_text(json.dumps({str(p.relative_to(D)):hashlib.sha256(p.read_bytes()).hexdigest()for p in files},indent=2)+'\n')
