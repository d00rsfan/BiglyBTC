from pathlib import Path
import json,shlex,subprocess
D=Path(__file__).resolve().parent;R=D.parents[1];N=R.parent/'BiglyBTC/preview/native';B=R/'build/public-shared-quota-runtime-20261121/native-gcc';O=D/'native-overlay';out=D/'gcc';out.mkdir(exist_ok=True);tasks=[]
for name,library in [('profile_source','biglybtc_owned_i2p_source'),('bridge','biglybtc_i2p_provider_owner'),('profile_owner','biglybtc_i2p_provider_owner')]:
 source=N/'services/i2p_provider'/(name+'.cpp');target='services/CMakeFiles/'+library+'.dir/i2p_provider/'+name+'.cpp.o';ls=subprocess.check_output(['ninja','-C',str(B),'-t','commands',target],text=True).splitlines();cmd=[x for x in ls if' -c '+str(source)in x];assert len(cmd)==1
 a=shlex.split(cmd[0]);a[a.index('-c')+1]=str(O/'services/i2p_provider'/(name+'.cpp'));a[a.index('-o')+1]=str(out/(name+'.o'));a[a.index('-MF')+1]=str(out/(name+'.d'));a=[('-I'+str(O)+x[len('-I'+str(N)):])if x.startswith('-I'+str(N))else x for x in a];a[1:1]=['-I'+str(O/'engine'),'-I'+str(O/'services/i2p_provider')];tasks.append(dict(name='private-owner-refresh-gcc-'+name+'-compile',argv=['timeout','500','nice','-n','10',*a],cwd=str(B)))
(D/'gcc-builds.json').write_text(json.dumps(tasks,indent=2)+'\n')
