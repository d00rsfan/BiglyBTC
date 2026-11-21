from pathlib import Path
import json,shlex,subprocess
D=Path(__file__).resolve().parent;R=D.parents[1];N=R.parent/'BiglyBTC/preview/native';B=R/'build/public-shared-quota-runtime-20261121/native-gcc';O=D/'native-overlay';out=D/'owner-gcc'
target='tests/CMakeFiles/biglybtc-private-sdk-bootstrap-tests.dir/private_sdk_bootstrap_tests.cpp.o';lines=subprocess.check_output(['ninja','-C',str(B),'-t','commands',target],text=True).splitlines();found=[line for line in lines if ' -c '+str(N/'tests/private_sdk_bootstrap_tests.cpp') in line];assert len(found)==1
argv=shlex.split(found[0]);argv[argv.index('-c')+1]=str(D/'owner_storage_tests.cpp');argv[argv.index('-o')+1]=str(out/'fixture.o');argv[argv.index('-MF')+1]=str(out/'fixture.d');argv[1:1]=['-I'+str(O/'tests'),'-I'+str(O/'services/i2p_provider'),'-I'+str(O/'engine'),'-I'+str(O/'adapters'),'-I'+str(O/'core/include'),'-I'+str(O/'app')]
link=shlex.split(subprocess.check_output(['ninja','-C',str(B),'-t','commands','biglybtc-private-sdk-bootstrap-tests'],text=True).splitlines()[-1]);assert link[:2]==[':','&&']and link[-2:]==['&&',':'];link=link[2:-2];at=link.index(target);link[at:at+1]=[str(out/'fixture.o'),str(out/'owner.o'),str(out/'source.o'),str(D/'probe-gcc/libprofile-cohort.a')];link[link.index('-o')+1]=str(out/'owner-storage-tests')
tasks=[dict(name='private-tag-owner-gcc-fixture-compile',argv=['timeout','300','nice','-n','10',*argv],cwd=str(B)),dict(name='private-tag-owner-gcc-link',argv=['timeout','180','nice','-n','10',*link],cwd=str(B))]
(D/'owner-test-build.json').write_text(json.dumps(tasks,indent=2)+'\n')
checks=[]
for mode in ['tagged','default-fenced','foreign-controller','held-gate','stale-config']:
 checks.append(dict(name='private-tag-owner-gcc-'+mode,argv=['timeout','90',str(out/'owner-storage-tests'),mode],env={'LD_LIBRARY_PATH':str(R/'build/tag-rate-cross-owner-20261121/sdk-install/lib')+':/opt/Qt/6.12.0/gcc_64/lib','QT_QPA_PLATFORM':'offscreen'}))
(D/'owner-checks.json').write_text(json.dumps(checks,indent=2)+'\n')
