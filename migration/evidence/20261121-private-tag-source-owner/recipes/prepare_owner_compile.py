from pathlib import Path
import json,shlex,subprocess
D=Path(__file__).resolve().parent;R=D.parents[1];N=R.parent/'BiglyBTC/preview/native';B=R/'build/public-shared-quota-runtime-20261121/native-gcc';O=D/'native-overlay';out=D/'owner-gcc';out.mkdir(exist_ok=True);tasks=[]
for name,source in [('source','profile_source.cpp'),('owner','profile_owner.cpp')]:
 library='biglybtc_owned_i2p_source' if name=='source' else 'biglybtc_i2p_provider_owner'
 target='services/CMakeFiles/'+library+'.dir/i2p_provider/'+source+'.o'
 lines=subprocess.check_output(['ninja','-C',str(B),'-t','commands',target],text=True).splitlines();matches=[line for line in lines if ' -c '+str(N/'services/i2p_provider'/source) in line];assert len(matches)==1
 argv=shlex.split(matches[0]);argv[argv.index('-c')+1]=str(O/'services/i2p_provider'/source);argv[argv.index('-o')+1]=str(out/(name+'.o'));argv[argv.index('-MF')+1]=str(out/(name+'.d'))
 include=['-I'+str(O/'services/i2p_provider'),'-I'+str(O/'engine'),'-I'+str(O/'adapters'),'-I'+str(O/'core/include'),'-I'+str(O/'services'),'-I'+str(O)]
 argv[1:1]=include;tasks.append(dict(name='private-tag-owner-gcc-'+name+'-compile',argv=['timeout','500','nice','-n','10',*argv],cwd=str(B)))
(D/'owner-compile.json').write_text(json.dumps(tasks,indent=2)+'\n')
