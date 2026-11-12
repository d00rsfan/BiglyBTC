#!/usr/bin/env python3
"""Abandoned/crashed startup must not replace checked pieces with RECHECK."""
import argparse,gzip,hashlib,os,pathlib,subprocess,tempfile
from state_cache_materialization_tests import encode,decode

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--driver',required=True);args=parser.parse_args()
    java=os.environ.get('BIGLYBTC_RESUME_JAVA');classpath=os.environ.get('BIGLYBTC_RESUME_CLASSPATH');checked=0;java_checked=0
    with tempfile.TemporaryDirectory(prefix='biglybtc-resume-transaction-') as root:
        root=pathlib.Path(root)
        for compressed in (False,True):
            for partial in (False,True):
                pieces=b'\1'*4+(b'\0'*4 if partial else b'\1'*4)
                info={b'name':b'generated.bin',b'length':8*16384,b'piece length':16384,b'pieces':b'z'*160}
                key=hashlib.sha1(encode(info)).hexdigest().upper()
                state={b'info':info,b'attributes':{b'resumecomplete':1 if partial else 2},b'resume':{b'data':{b'valid':1,b'resume data':pieces,b'blocks':{}}},b'opaque':b'\0\xffkeep'}
                for stage in ('abandon','temp','backup','primary','normal','old-destructive-control'):
                    profile=root/(str(compressed)+'-'+str(partial)+'-'+stage);active=profile/'active';active.mkdir(parents=True)
                    filename=active/(key+'.dat');raw=encode(state);filename.write_bytes(gzip.compress(raw,mtime=0) if compressed else raw)
                    command='begin' if stage=='old-destructive-control' else 'resume-stage-abandon' if stage=='abandon' else 'resume-stage-save'
                    argv=[args.driver,str(profile),key,command]+([] if stage in ('abandon','old-destructive-control') else [stage])
                    done=subprocess.run(argv,capture_output=True,timeout=15);expected=66 if stage in ('temp','backup','primary') else 0
                    assert done.returncode==expected,(stage,done.returncode,done.stderr.decode(errors='replace'))
                    actual=decode(filename.read_bytes());resume=actual[b'resume'][b'data']
                    committed=stage in ('primary','normal');expected_pieces=pieces.replace(b'\0',b'\2') if committed else pieces
                    if stage=='old-destructive-control':expected_pieces=b'\2'*8
                    assert resume[b'resume data']==expected_pieces,(stage,partial,'checked piece map changed')
                    expected_valid=0 if stage=='old-destructive-control' or (partial and committed) else 1
                    assert resume[b'valid']==expected_valid,(stage,'validity changed')
                    assert actual[b'info']==info and actual[b'opaque']==state[b'opaque']
                    if stage=='abandon':assert not pathlib.Path(str(filename)+'.bak').exists()
                    if java and classpath:
                        result=subprocess.run([java,'-XX:ActiveProcessorCount=2','-Dazureus.config.path='+str(root/'java-profile'),'-cp',classpath,'ResumeStateOracle',str(filename),expected_pieces.hex(),str(expected_valid)],capture_output=True,timeout=15)
                        assert result.returncode==0,(stage,'original Java decode',result.stderr.decode(errors='replace'))
                        java_checked+=1
                    checked+=1
    print('PASS resume transaction cases='+str(checked)+' original_java_reads='+str(java_checked))
if __name__=='__main__':main()
