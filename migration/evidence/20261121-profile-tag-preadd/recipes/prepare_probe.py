from pathlib import Path
import hashlib,json,shlex,subprocess
D=Path(__file__).resolve().parent;R=D.parents[1];N=R.parent/'BiglyBTC/preview/native';B=R/'build/public-shared-quota-runtime-20261121/native-gcc'
target='tests/CMakeFiles/biglybtc-profile-tag-rate-cohort-tests.dir/profile_cohort_tests.cpp.o'
lines=subprocess.check_output(['ninja','-C',str(B),'-t','commands',target],text=True).splitlines();matches=[x for x in lines if ' -c '+str(N/'tests/profile_cohort_tests.cpp') in x];assert len(matches)==1
argv=shlex.split(matches[0]);argv[argv.index('-o')+1]=str(D/'fixture-gcc.o');argv[argv.index('-MF')+1]=str(D/'fixture-gcc.d');argv[argv.index('-c')+1]=str(D/'profile_preadd_tests.cpp');argv[1:1]=['-I'+str(D/'drafts'),'-I'+str(N/'tests')]
tasks=[dict(name='profile-preadd-gcc-fixture-compile',argv=['timeout','280','nice','-n','10',*argv],cwd=str(B))]
line=subprocess.check_output(['ninja','-C',str(B),'-t','commands','biglybtc-profile-tag-rate-cohort-tests'],text=True).splitlines()[-1];link=shlex.split(line);assert link[:2]==[':','&&'] and link[-2:]==['&&',':'];link=link[2:-2]
at=link.index(target);link[at:at+1]=[str(D/'fixture-gcc.o'),str(D/'profile-gcc.o')];link[link.index('-o')+1]=str(D/'profile-preadd-gcc')
tasks.append(dict(name='profile-preadd-gcc-link',argv=['timeout','180','nice','-n','10',*link],cwd=str(B)))
(D/'variant-builds.json').write_text(json.dumps(tasks,indent=2)+'\n')
pins={}
for arg in link:
 if arg.endswith('.a'):
  p=Path(arg);p=p if p.is_absolute() else B/p;pins[str(p)]=hashlib.sha256(p.read_bytes()).hexdigest()
(D/'probe-release-archives.json').write_text(json.dumps(pins,indent=2)+'\n')
checks=[]
for mode in ['bound','empty','stale','close','damaged','foreign','cohort','worker','sdk-abort','no-lease']:
 checks.append(dict(name='profile-preadd-gcc-'+mode,argv=['timeout','75',str(D/'profile-preadd-gcc'),mode],env={'LD_LIBRARY_PATH':str(R/'build/tag-rate-cross-owner-20261121/sdk-install/lib')+':/opt/Qt/6.12.0/gcc_64/lib','QT_QPA_PLATFORM':'offscreen'}))
(D/'probe-checks.json').write_text(json.dumps(checks,indent=2)+'\n')
