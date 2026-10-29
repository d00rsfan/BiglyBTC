#!/usr/bin/env python3
"""Compile only the Clang HTTP test and measure the one failed admitted TLS row."""
import importlib.util,json,pathlib
R=pathlib.Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';H=pathlib.Path(__file__).parent
spec=importlib.util.spec_from_file_location('s',R/'build/mainline-scrape-20261028/recipe_support_followup.py');s=importlib.util.module_from_spec(spec);spec.loader.exec_module(s)
source=s.load(H/'diagnostic-adoption.json')['source_commit'];before=s.source_snapshot(source)
out=H/'runs/clang-diagnostic-r1';out.mkdir(parents=True,exist_ok=False);env=s.environment('clang')
for key in ('BIGLYBTC_PREVIEW_PROFILE','BIGLYBTC_HTTP_PRIVATE_RUNTIME','ASAN_OPTIONS','LSAN_OPTIONS','UBSAN_OPTIONS','QT_TLS_BACKEND','LD_DEBUG_OUTPUT'):env.pop(key,None)
for key in ('HOME','XDG_CONFIG_HOME','XDG_CACHE_HOME','XDG_RUNTIME_DIR'):
 p=out/key.lower();p.mkdir(mode=0o700);env[key]=str(p)
runner=s.Runner(out,env);cache=N/'build/saved-services-20261018/clang';target=cache/'tests/biglybtc-mainline-single-http-tests';result={'status':'FAIL_DIAGNOSTIC','source_commit':source,'qualification_claimed':False}
try:
 runner.run('build',['nice','-n','5','cmake','--build',cache,'--parallel','1','--target','biglybtc-mainline-single-http-tests'],timeout=900)
 result['test_elf_sha256']=s.sha(target)
 runner.run('selected',[target,'normalAdmittedProxyUsesActualAnnounceOrSoleScrapeAndWithdrawsBeforeLateDelivery:private-HTTPS-saved-JKS-real-scrape','-o',str(out/'selected.txt')+',txt','-o',str(out/'selected.xml')+',junitxml'],timeout=120)
 result['status']='PASS_SINGLE_DIAGNOSTIC_ROW'
except BaseException as exc:result['failure']=repr(exc)
finally:
 result['source_unchanged']=s.source_snapshot(source)==before;s.write(out/'result.json',result)
print(result['status'],out/'result.json',flush=True)
