from pathlib import Path
import hashlib,json,re
D=Path(__file__).resolve().parent;base=json.loads((D/'owner-compile.json').read_text())+json.loads((D/'owner-test-build.json').read_text());tasks=[];checks=[]
for variant,compiler,flags in [('san','/usr/bin/g++-16',['-O1','-g1','-fsanitize=address,undefined','-fno-omit-frame-pointer']),('clang','/usr/bin/clang++-24',['--gcc-install-dir=/usr/lib/gcc/x86_64-linux-gnu/16'])]:
 out=D/('owner-'+variant);out.mkdir(exist_ok=True)
 for old in base:
  task=json.loads(json.dumps(old));task['name']=task['name'].replace('-gcc-','-'+variant+'-');a=task['argv'];index=next(i for i,x in enumerate(a) if Path(x).name in ['c++','g++-16']);a[index]=compiler
  a=[x.replace(str(D/'owner-gcc'),str(out)).replace(str(D/'probe-gcc/libprofile-cohort.a'),str(D/f'probe-{variant}/libprofile-cohort.a')) for x in a]
  if variant=='san':a=[x for x in a if not re.fullmatch(r'-O[0-3sg]|-g[0-3]?',x)]
  a=[('-I'+str(D/'native-overlay')+x[len('-I/home/yurii/Java/BiglyBTC/preview/native'):]) if x.startswith('-I/home/yurii/Java/BiglyBTC/preview/native') else x for x in a]
  a[index+1:index+1]=flags;task['argv']=a;tasks.append(task)
 for old in json.loads((D/'owner-checks.json').read_text()):
  task=json.loads(json.dumps(old));task['name']=task['name'].replace('-gcc-','-'+variant+'-');task['argv']=[x.replace('/owner-gcc/','/owner-'+variant+'/') for x in task['argv']];task['env'].update(ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1');checks.append(task)
(D/'owner-variant-builds.json').write_text(json.dumps(tasks,indent=2)+'\n');(D/'owner-variant-checks.json').write_text(json.dumps(checks,indent=2)+'\n')
inputs={}
for path in ['drafts/profile_tag_rates.cpp','drafts/sdk_tag_rate_group.cpp','drafts/sdk_prepared_tag_attachment.hpp','drafts/profile_prepared_tag_attachment.hpp','drafts/private_tag_rate_source.hpp','native-overlay/services/i2p_provider/profile_source.cpp','native-overlay/services/i2p_provider/profile_source.hpp','native-overlay/services/i2p_provider/profile_owner.cpp','native-overlay/services/i2p_provider/profile_owner.hpp','private_source_tests.cpp','owner_storage_tests.cpp']:
 inputs[path]=hashlib.sha256((D/path).read_bytes()).hexdigest()
(D/'prototype-inputs.json').write_text(json.dumps(inputs,indent=2)+'\n');print('Prepared3 actual private owner/source/fixture TUs per scoped SAN/Clang;Release dependencies explicit')
