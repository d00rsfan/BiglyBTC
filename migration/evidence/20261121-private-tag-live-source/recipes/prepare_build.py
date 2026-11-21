from pathlib import Path
import copy,json,shlex,subprocess
D=Path(__file__).resolve().parent;R=D.parents[1];P=D.parent/'private-tag-source-20261121';Q=D.parent/'profile-common-tag-owner-20261121';N=R.parent/'BiglyBTC/preview/native';O=D/'native-overlay';B=P/'probe-gcc';out=D/'gcc';out.mkdir(exist_ok=True)
t=json.loads((Q/'gcc-qualified-builds.json').read_text())[0];t=copy.deepcopy(t);t['name']='private-tag-live-gcc-controller-compile';t['argv']=[x.replace(str(Q/'native-overlay-final'),str(O)).replace(str(Q/'gcc-final'),str(out))for x in t['argv']]
lines=subprocess.check_output(['ninja','-C',str(B),'-t','commands','private-source-controls'],text=True).splitlines();cmd=[s for s in lines if ' -c '+str(P/'private_source_tests.cpp')in s];assert len(cmd)==1
argv=shlex.split(cmd[0]);argv[argv.index('-c')+1]=str(D/'live_source_tests.cpp');argv[argv.index('-o')+1]=str(out/'fixture.o');argv[argv.index('-MF')+1]=str(out/'fixture.d')
argv=[('-I'+str(O)+x[len('-I'+str(N)):])if x.startswith('-I'+str(N))else x for x in argv];argv[1:1]=['-I'+str(O/'engine'),'-I'+str(O/'adapters'),'-I'+str(O/'tests'),'-I'+str(O/'core/include')];argv=[x for x in argv if x not in ['-I'+str(P/'drafts')]]
fixture=dict(name='private-tag-live-gcc-fixture-compile',argv=['timeout','240','nice','-n','10',*argv],cwd=str(B))
link=shlex.split(lines[-1]);assert link[:2]==[':','&&']and link[-2:]==['&&',':'];link=link[2:-2];old='CMakeFiles/private-source-controls.dir/private_source_tests.cpp.o';assert old in link;at=link.index(old);link[at:at+1]=[str(out/'fixture.o'),str(out/'profile_tag_rates.o')];link[link.index('-o')+1]=str(out/'private-tag-live-tests')
tasks=[t,fixture,dict(name='private-tag-live-gcc-link',argv=['timeout','180','nice','-n','10',*link],cwd=str(B))];(D/'gcc-builds.json').write_text(json.dumps(tasks,indent=2)+'\n')
modes=['current','rate','disabled','removed-tag','empty','cohort','sibling-abort','foreign','remove','gate','saving','close'];env=dict(LD_LIBRARY_PATH=str(R/'build/tag-rate-cross-owner-20261121/sdk-install/lib')+':/opt/Qt/6.12.0/gcc_64/lib',QT_QPA_PLATFORM='offscreen');(D/'gcc-checks.json').write_text(json.dumps([dict(name='private-tag-live-gcc-'+m,argv=['timeout','90',str(out/'private-tag-live-tests'),m],env=env)for m in modes],indent=2)+'\n')
print('Prepared12 current bound-source controls,isolated controller and frozen D9 wrapper archive')
