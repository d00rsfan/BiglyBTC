#!/usr/bin/env python3
"""Root-only javac-once preparation; no execution on import."""
import argparse,importlib.util,pathlib,re
W=pathlib.Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('window_batch',W/'run.py');b=importlib.util.module_from_spec(spec);spec.loader.exec_module(b);s=b.s

def main():
 p=argparse.ArgumentParser();p.add_argument('--attempt',required=True);p.add_argument('--source-commit',required=True);p.add_argument('--adoption',type=pathlib.Path,required=True)
 p.add_argument('--java',type=pathlib.Path,default=pathlib.Path('/usr/bin/java'));p.add_argument('--javac',type=pathlib.Path,default=pathlib.Path('/usr/bin/javac'));a=p.parse_args()
 assert re.fullmatch('[A-Za-z0-9_-]+',a.attempt) and a.adoption.is_absolute() and a.java.is_absolute() and a.javac.is_absolute()
 adoption=s.load(a.adoption);assert adoption['source_commit']==a.source_commit;snapshot=s.source_snapshot(a.source_commit)
 assert s.sha(b.ACCEPTANCE)==b.ACCEPTANCE_SHA;cap=s.load(b.ACCEPTANCE)
 for name,pin in cap['artifacts_sha256'].items():assert s.sha(b.ACCEPTANCE.parent/name)==pin
 for name,row in adoption['source_paths'].items():assert s.sha(b.N/name)==row['after_sha256']
 helper=pathlib.Path(cap['helper_source']['path']);assert s.sha(helper)==cap['helper_source']['sha256'] and s.sha(b.REFERENCE_JAR)==b.base.JAR_SHA
 out=W/'java-runs'/a.attempt;out.mkdir(parents=True,exist_ok=False);classes=out/'classes';classes.mkdir();env=s.environment()
 for key in ('JAVA_TOOL_OPTIONS','_JAVA_OPTIONS','JDK_JAVA_OPTIONS','CLASSPATH','LD_DEBUG_OUTPUT','ASAN_OPTIONS','LSAN_OPTIONS','UBSAN_OPTIONS'):env.pop(key,None)
 for key in ('HOME','TMPDIR','XDG_CONFIG_HOME','XDG_CACHE_HOME'):
  d=out/key.lower();d.mkdir(mode=0o700);env[key]=str(d)
 runner=s.Runner(out,env);result={'status':'FAIL','source_commit':a.source_commit,'adoption_sha256':s.sha(a.adoption),'runner_sha256':s.sha(__file__),'acceptance_manifest_sha256':b.ACCEPTANCE_SHA}
 try:
  pins=b.base.file_pins([a.java,a.javac,b.REFERENCE_JAR,helper,W/'prepare.py',W/'run.py',b.BASE]);s.write(out/'inputs.json',pins)
  runner.run('java-version',[a.java,'-version'],timeout=30);runner.run('javac-version',[a.javac,'-version'],timeout=30)
  runner.run('javac',['nice','-n','5',a.javac,'-J-Xms16m','-J-Xmx128m','-J-XX:ActiveProcessorCount=1','-J-XX:+UseSerialGC','--release','17','-cp',b.REFERENCE_JAR,'-d',classes,helper],timeout=180)
  actual={str(p.relative_to(classes)):s.sha(p) for p in classes.rglob('*.class')};assert set(actual)=={'StreamingPeer.class','StreamingPeer$Adapter.class'}
  assert s.source_snapshot(a.source_commit)==snapshot and b.base.file_pins(pins)==pins
  result.update(status='PASS_PREPARED_STREAMING_WINDOW_HELPER',java_program=str(a.java),classes_directory=str(classes),classes=actual,java=b.base.file_pins([a.java]),javac=b.base.file_pins([a.javac]),jar=b.base.file_pins([b.REFERENCE_JAR]),helper_source=str(helper),helper_source_sha256=s.sha(helper),source_unchanged=True,subjects_executed=False)
 except BaseException as exc:result['failure']=repr(exc)
 finally:result['commands_sha256']=s.sha(out/'commands.json') if (out/'commands.json').exists() else None;s.write(out/'result.json',result)
 print(result['status'],out/'result.json',flush=True);return 0 if result['status'].startswith('PASS_') else 1
if __name__=='__main__':raise SystemExit(main())
