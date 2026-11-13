#!/usr/bin/env python3
"""Replay immutable original policy observations without accumulating files."""
import argparse,hashlib,json,pathlib,subprocess
def sha(path):return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()
def run_cases(driver,cases,prefix):
    driver=pathlib.Path(driver).resolve(strict=True);before=sha(driver);actual=hashlib.sha256();observations=0
    for specification in cases:
        name,arguments,commands,expected=specification[:4];wanted=specification[4] if len(specification)==5 else 0
        assert len(commands)<=4*1024*1024 and len(expected)<=16*1024*1024,"Bounded generated case required"
        result=subprocess.run([str(driver),"--stdio",*arguments],input=commands,capture_output=True,timeout=120)
        assert result.returncode==wanted,(name,"driver exit",result.returncode,wanted,result.stderr[:2000])
        if result.stdout!=expected:
            a,e=result.stdout.splitlines(),expected.splitlines();first=next((i for i,pair in enumerate(zip(a,e)) if pair[0]!=pair[1]),min(len(a),len(e)))
            raise AssertionError((name,"policy mismatch",first+1,a[first:first+1],e[first:first+1]))
        actual.update(result.stdout);observations+=max(1,len(commands.splitlines()))
    assert sha(driver)==before,"Native driver changed during replay"
    return {"actual_sha256":actual.hexdigest(),"observations":observations,"temporary_files_retained":False}
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument("driver",type=pathlib.Path);parser.add_argument("--fixtures",type=pathlib.Path,required=True);args=parser.parse_args();manifest=json.loads((args.fixtures/"manifest.json").read_text())
    assert manifest["azrating"]=="1629340340d0bc391d9e582f9ee989a93d550581" and manifest["biglybt"]=="cb8c6cb9bb97916cbc7e80338a017200ffc3e71f"
    assert manifest["original_manifest_sha256"]=="88c89f6c60f8488cc6164da13d477a560784c99232a5abdc1afda6d91fe4c162"
    assert manifest["source_only_observations"]==193;eligible={"queue-dispatch":362,"completion-timer":177,"removal-stop":114};assert set(manifest["groups"])==set(eligible);cases=[]
    for group,count in eligible.items():
        record=manifest["groups"][group];assert record["observations"]==count
        for name,digest in record["sha256"].items():assert name in {"commands.txt","expected.txt"} and sha(args.fixtures/group/name)==digest
        commands=(args.fixtures/group/"commands.txt").read_bytes();expected=(args.fixtures/group/"expected.txt").read_bytes();assert len(commands.splitlines())==count and len(expected.splitlines())==count;cases.append((group,[],commands,expected))
    report=run_cases(args.driver,cases,"");assert report["observations"]==653
    print("PASS 653 frozen original rating publication observations;193 source-only rows excluded; no temporary files")
if __name__=="__main__":main()
