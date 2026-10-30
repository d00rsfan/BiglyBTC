"""Incremental diagnostic owner/probe only; ordinary tested executables stay pinned."""
import importlib.util,json,os,pathlib,re,stat,sys
sys.dont_write_bytecode=True
P=pathlib.Path(__file__).resolve().parent;T=P.parents[2];W=T/'native-followup-20261030';D=T/'diagnostics/tracker-state-observation'
spec=importlib.util.spec_from_file_location('recipe',W/'amendments/payload-fixture-layout/run.py');b=importlib.util.module_from_spec(spec);spec.loader.exec_module(b);a=b.a;s=b.s
adoption=a.load(P/'adoption.json');source=adoption['source_commit'];prior=D/'runs/r1';before=a.snapshot(source);expected=dict(a.load(prior/'source-before.json')['files_sha256'])
for rel,row in adoption['source_paths'].items():expected[rel]=row['after_sha256']
assert before['files_sha256']==expected
fifo=re.search(r'--jobserver-auth=fifo:([^\s]+)',os.environ.get('MAKEFLAGS',''));assert fifo and stat.S_ISFIFO(pathlib.Path(fifo[1]).stat().st_mode)
images=a.load(prior/'images-after.json');paths={k:pathlib.Path(v['path']) for k,v in images.items()};assert s.images(paths)==images
protected=a.protected();shipping=a.shipping();dist=a.sha(a.N/'dist/BUILD_MANIFEST.json');out=P/'runs/r1';out.mkdir(parents=True,exist_ok=False);env=s.environment();runner=s.Runner(out,env);cache=W/'caches/gcc'
a.write(out/'source-before.json',before);a.write(out/'origin-images.json',images)
try:
 runner.run('build',['nice','-n','5','cmake','--build',cache,'--target',b.OWNER,b.PROBE,b.SUITES['pure-i2p-streaming-manager'],'--','-k','0'],timeout=7200)
 runner.run('manager-regression',['ctest','--test-dir',cache,'-R','^pure-i2p-streaming-manager$','--output-on-failure','--output-junit',out/'tests.xml'],timeout=1000)
 after=s.images(paths);assert {k:v for k,v in after.items() if k not in (b.PROBE,b.SUITES['pure-i2p-streaming-manager'])}=={k:v for k,v in images.items() if k not in (b.PROBE,b.SUITES['pure-i2p-streaming-manager'])}
 assert a.snapshot(source)==before and a.protected()==protected and a.shipping()==shipping and a.sha(a.N/'dist/BUILD_MANIFEST.json')==dist
 a.write(out/'images-before.json',after);a.write(out/'images-after.json',s.images(paths));a.write(out/'source-after.json',a.snapshot(source));a.write(out/'archives-after.json',{str(cache/'services/libbiglybtc_i2p_provider_owner.a'):a.sha(cache/'services/libbiglybtc_i2p_provider_owner.a')})
 result={'status':'PASS_PORT_ZERO_REGRESSION','lane':'gcc','cache':str(cache),'source_commit':source,'source_unchanged':True,'images_unchanged':True,'actual_suites':1,'ordinary_suite_origin':a.ref(W/'amendments/payload-fixture-layout/runs/gcc-r3/result.json'),'other_10_ordinary_images_unchanged':True,'outgoing_port_zero_fix':True,'shipping_provider_unchanged':True,'dist_unchanged':True,'actual_transfer':False}
except BaseException as error:result={'status':'FAIL','failure':repr(error)}
a.write(out/'result.json',result);print(result['status'],flush=True);sys.exit(0 if result['status']=='PASS_PORT_ZERO_REGRESSION' else 1)
