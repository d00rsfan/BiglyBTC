from pathlib import Path
import json,shlex,subprocess
D=Path(__file__).resolve().parent;R=D.parents[1];N=R.parent/'BiglyBTC/preview/native';B=R/'build/public-shared-quota-runtime-20261121/native-gcc';O=D/'native-overlay';out=D/'gcc';out.mkdir(exist_ok=True);tasks=[]
for name in ['profile_tag_rates','persistent_session','owned_i2p_service']:
 area='app' if name=='owned_i2p_service' else 'engine';target=f'app/CMakeFiles/biglybtc_desktop_host.dir/{name}.cpp.o' if area=='app' else f'engine/CMakeFiles/biglybtc_local_engine.dir/{name}.cpp.o';lines=subprocess.check_output(['ninja','-C',str(B),'-t','commands',target],text=True).splitlines();found=[line for line in lines if ' -c '+str(N/area/(name+'.cpp')) in line];assert len(found)==1
 argv=shlex.split(found[0]);argv[argv.index('-c')+1]=str(O/area/(name+'.cpp'));argv[argv.index('-o')+1]=str(out/(name+'.o'));argv[argv.index('-MF')+1]=str(out/(name+'.d'));argv=[('-I'+str(O)+x[len('-I'+str(N)):]) if x.startswith('-I'+str(N)) else x for x in argv];argv[1:1]=['-I'+str(O/'engine'),'-I'+str(O/'adapters'),'-I'+str(O/'core/include')];tasks.append(dict(name='common-tag-gcc-'+name+'-compile',argv=['timeout','600','nice','-n','10',*argv],cwd=str(B)))
(D/'gcc-builds.json').write_text(json.dumps(tasks,indent=2)+'\n')
