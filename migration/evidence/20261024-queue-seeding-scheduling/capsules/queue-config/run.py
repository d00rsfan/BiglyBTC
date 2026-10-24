#!/usr/bin/env python3
import argparse, hashlib, json, os, shutil, subprocess, sys, tempfile
from pathlib import Path
sys.dont_write_bytecode = True
ROOT = Path('/home/yurii/Java/BiglyBT')
NATIVE = ROOT.parent / 'BiglyBTC'
HERE = Path(__file__).resolve().parent
JAVA = Path('/usr/lib/jvm/java-17-openjdk-amd64/bin')
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--driver',type=Path,required=True);parser.add_argument('--attempt',required=True)
    args=parser.parse_args();destination=HERE/args.attempt
    assert not destination.exists() and args.attempt.startswith('oracle-') and '/' not in args.attempt
    cohort=Path('/tmp/biglybtc-oracle/classes')
    retention=NATIVE/'build/dependencies/java-oracle-17-20261021/retention.json'
    retained=json.loads(retention.read_text())
    assert retained['source_commit']=='cb8c6cb9bb97916cbc7e80338a017200ffc3e71f' and not retained['recompiled']
    def inventory():
        result={}
        for p in cohort.rglob('*'):
            assert not p.is_symlink()
            if p.is_file(): result[p.relative_to(cohort).as_posix()]=sha(p)
        return result
    assert inventory()==retained['file_sha256']
    work=Path(tempfile.mkdtemp(prefix='biglybtc-queue-settings-oracle-',dir='/tmp'))
    commands=[];env={k:v for k,v in os.environ.items() if k not in ['CLASSPATH','JAVA_TOOL_OPTIONS','_JAVA_OPTIONS','JDK_JAVA_OPTIONS','LD_PRELOAD']}
    for name in ['classes','empty-sourcepath','payload']: (work/name).mkdir()
    def run(name,argv):
        command={'name':name,'argv':list(map(str,argv))};commands.append(command)
        with (work/(name+'.stdout')).open('w') as out,(work/(name+'.stderr')).open('w') as err:
            result=subprocess.Popen(command['argv'],cwd=work,env=env,stdout=out,stderr=err)
            command['pid']=result.pid
            (work/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
            try:
                result.wait(timeout=20)
            except subprocess.TimeoutExpired:
                if str(argv[0]).endswith('/java'):
                    with (work/(name+'.threads')).open('w') as trace:
                        subprocess.run([str(JAVA/'jstack'),str(result.pid)],stdout=trace,stderr=subprocess.STDOUT,timeout=15)
                try: result.wait(timeout=55)
                except subprocess.TimeoutExpired:
                    result.kill();result.wait();raise
        command['exit_code']=result.returncode
        (work/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
        assert result.returncode==0,name
    try:
        run('javac-helpers',[JAVA/'javac','-J-XX:ActiveProcessorCount=2','--release','8','-encoding','UTF-8','-proc:none','-implicit:none','-sourcepath',work/'empty-sourcepath','-cp',cohort,'-d',work/'classes',ROOT/'migration/fixtures/java/ReferenceFence.java',HERE/'QueueSettingsOracle.java'])
        sys.path.insert(0,str(ROOT/'migration/tools'))
        from peer_interop import jvm
        classpath=str(work/'classes')+':'+str(cohort)
        run('java-create',[*jvm(work,classpath),'QueueSettingsOracle','create'])
        shutil.copy2(work/'profile/biglybt.config',work/'original-created.config')
        run('native-apply',[args.driver,work/'profile','apply'])
        shutil.copy2(work/'profile/biglybt.config',work/'native-edited.config')
        run('java-reload-save',[*jvm(work,classpath),'QueueSettingsOracle','check'])
        shutil.copy2(work/'profile/biglybt.config',work/'original-resaved.config')
        run('native-reload',[args.driver,work/'profile','check'])
        assert inventory()==retained['file_sha256']
        expected={'java-create':'QUEUE_SETTINGS_ORIGINAL_CREATE_PASS','native-apply':'QUEUE_SETTINGS_NATIVE_apply_PASS','java-reload-save':'QUEUE_SETTINGS_ORIGINAL_CHECK_PASS','native-reload':'QUEUE_SETTINGS_NATIVE_check_PASS'}
        for name,marker in expected.items():
            assert marker in (work/(name+'.stdout')).read_text(),name
        record={'status':'PASS_ACTUAL_JAVA_NATIVE_JAVA_NATIVE_QUEUE_SETTINGS','original_baseline':retained['source_commit'],
            'native_driver_sha256':sha(args.driver),'retention_sha256':sha(retention),'cohort_files':len(retained['file_sha256']),
            'original_core_recompiled':False,'original_profile_or_process_access':False,
            'scope':'Actual original COConfigurationManager queue getters/saver and native100-field sparse adapter plus readQueuePolicy; generated configuration, no original core/network startup',
            'conditional_first_priority_ignore_zero_peers':False,'conditional_flag_materialized':False,'actual_handoff_phases':4,
            'commands':commands,'fixture_sha256':{p.name:sha(p) for p in work.glob('*.config')},
            'logs_sha256':{p.name:sha(p) for p in work.glob('*.std*')}}
        (work/'result.json').write_text(json.dumps(record,indent=2)+'\n')
        print(record['status'],flush=True)
    finally:
        shutil.copytree(work,destination)
        print('All oracle data retained at',destination,flush=True)
if __name__=='__main__':main()
