"""Generate isolated rebuild recipes for the retained expected-failure bodies."""
from pathlib import Path
import json
D=Path(__file__).resolve().parent;out=D/'before-replay';out.mkdir(exist_ok=True)
base=json.loads((D/'gcc-compile.json').read_text())[0];fixture,link=json.loads((D/'variant-builds.json').read_text());tasks=[]
def replace_arg(argv,flag,value):argv[argv.index(flag)+1]=str(value)
a=fixture['argv'].copy();replace_arg(a,'-o',out/'fixture.o');replace_arg(a,'-MF',out/'fixture.d');tasks.append(dict(name='profile-preadd-before-replay-fixture',argv=a,cwd=fixture['cwd']))
for name in ['held-gate','abandoned']:
 a=base['argv'].copy();replace_arg(a,'-c',D/f'before-{name}-profile.cpp');replace_arg(a,'-o',out/(name+'.o'));replace_arg(a,'-MF',out/(name+'.d'));tasks.append(dict(name='profile-preadd-before-replay-'+name+'-compile',argv=a,cwd=base['cwd']))
 a=link['argv'].copy();a=[str(out/'fixture.o') if x==str(D/'fixture-gcc.o') else str(out/(name+'.o')) if x==str(D/'profile-gcc.o') else x for x in a];replace_arg(a,'-o',out/(name+'-tests'));tasks.append(dict(name='profile-preadd-before-replay-'+name+'-link',argv=a,cwd=link['cwd']))
(D/'before-replay-builds.json').write_text(json.dumps(tasks,indent=2)+'\n')
print('Replay bodies compiled against matching headers;run held-gate on first and abandoned/abandoned-installed on second;each expected exit1')
