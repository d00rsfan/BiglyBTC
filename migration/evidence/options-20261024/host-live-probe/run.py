from pathlib import Path
import hashlib,json,os,shlex,subprocess,time

NATIVE=Path('/home/yurii/Java/BiglyBTC')
BUILD=NATIVE/'build/persistent-peer-accounting-20261021/bound-gcc'
OUT=Path(__file__).resolve().parent
SOURCE=OUT/'options_host_live_probe.cpp'
assert hashlib.sha256(SOURCE.read_bytes()).hexdigest()=='40777a5640b1f592088a0d5365154bb14312225f8679697b2a3def40143b52ec'
# Caller runs only after the three-job sanitizer compilation has finished.
receipt=json.loads((OUT.parent/'options-sanitized-receipt.json').read_text())
assert len(receipt['commands'])>=2 and all(c['exit']==0 for c in receipt['commands'][:2]), 'Wait for sanitizer compilation to finish'
raw=subprocess.check_output(['ninja','-C',str(BUILD),'-t','commands','tests/biglybtc-options-host-tests'],text=True).splitlines()
def argv(line):
    result=shlex.split(line)
    if result[:2]==[':','&&']:result=result[2:]
    if result[-2:]==['&&',':']:result=result[:-2]
    assert '&&' not in result and ';' not in result
    return result
compile_lines=[line for line in raw if ' -c ' in line and '/options_host_tests.cpp' in line]
assert len(compile_lines)==1
compile_args=argv(compile_lines[0]);obj=OUT/'probe.o';binary=OUT/'options-host-live-probe'
compile_args[compile_args.index('-o')+1]=str(obj)
indices=[i for i,a in enumerate(compile_args) if Path(a).name=='options_host_tests.cpp'];assert len(indices)==1
compile_args[indices[0]]=str(SOURCE)
link=argv(raw[-1]);old=[a for a in link if a.startswith('tests/CMakeFiles/biglybtc-options-host-tests.dir/') and a.endswith('.o')]
assert len(old)==2,old
link=[a for a in link if a not in old];link[link.index('-o')+1]=str(binary);link.insert(1,str(obj))
commands=[]
def run(name,args,timeout,env=None):
    start=time.monotonic()
    with (OUT/(name+'.log')).open('w') as log:
        p=subprocess.run(args,cwd=BUILD,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=timeout)
    commands.append({'name':name,'argv':args,'exit_code':p.returncode,'seconds':round(time.monotonic()-start,2)})
    (OUT/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
    assert p.returncode==0,name
run('compile',['nice','-n5',*compile_args],45)
run('link',['nice','-n5',*link],45)
env=os.environ.copy();env['QT_QPA_PLATFORM']='offscreen'
run('actual-host-live',[str(binary),'/tmp/biglybtc-options-host-live-20261024-r2'],60,env)
print((OUT/'actual-host-live.log').read_text()[-2200:])
