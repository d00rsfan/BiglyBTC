#!/usr/bin/env python3
"""Root-run exact source adoption; imports never write or build."""
import argparse,hashlib,json,os,pathlib,re,subprocess
R=pathlib.Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';W=pathlib.Path(__file__).resolve().parent
BASIS='fd2c07c553dd9e092ea8812bbbbaea429a49d71f'
BINDING=N/'preview/native/engine/runtime-binding/provider-mainline-routing-runtime.json'
def sha(path):
 h=hashlib.sha256()
 with pathlib.Path(path).open('rb') as f:
  for block in iter(lambda:f.read(1048576),b''):h.update(block)
 return h.hexdigest()
def load(path):return json.loads(pathlib.Path(path).read_bytes())
def write(path,value):pathlib.Path(path).write_text(json.dumps(value,indent=2)+'\n')
def ref(path):return {'path':str(path),'sha256':sha(path)}
def git(*args):return subprocess.check_output(['git','-C',str(N),*args],text=True).strip()
def protected():return {'head':subprocess.check_output(['git','-C',str(R),'rev-parse','HEAD'],text=True).strip(),'index_sha256':sha(R/'.git/index')}
def pins():
 cap=load(W/'manifest.json')
 for name,pin in cap['artifacts_sha256'].items():assert sha(W/name)==pin,name
 for name,pin in cap['inputs_sha256'].items():assert sha(name)==pin,name
 plan=load(W/'source-plan.json')
 for name,row in plan['source_paths'].items():assert sha(row['after_path'])==row['after_sha256'],name
 return plan

def snapshot(source):
 assert re.fullmatch('[0-9a-f]{40}',source) and git('rev-parse','HEAD')==source and not git('status','--porcelain','--untracked-files=no')
 roots=['preview/native','preview/migration/tools','migration/tools','.github/workflows','plugin-sdk']
 assert not git('ls-files','--others','--exclude-standard','--',*roots)
 names=subprocess.check_output(['git','-C',str(N),'ls-files','-z','--',*roots]).decode().split('\0')
 return {'source_commit':source,'native_tree':git('rev-parse','HEAD:preview/native'),'files_sha256':{n:sha(N/n) for n in names if n and (N/n).is_file()}}

def shipping():
 assert sha(BINDING)=='f746fd279755ea90f31e20b0c9131c46e5f0871c4aab3665f232975f98feb56e'
 row=load(BINDING);record=BINDING.parent/row['source_build_record'];assert sha(record)==row['source_build_record_sha256']=='623778fc3a5b25144e842a39154dab4cd107e4f115bc6474dd973f67888582d3'
 assert sha(row['artifact'])==row['artifact_sha256']=='355f0c7bca7404c3199e85d4741a3734ac460c79d641a2539c049c5a8ad0c443'
 install=pathlib.Path(row['install_root']);installed={str(p.relative_to(install)):sha(p) for p in install.rglob('*') if p.is_file()};assert installed==load(record)['installed_sha256']
 return {'binding':ref(BINDING),'record':ref(record),'artifact':ref(row['artifact']),'install_root':str(install),'installed_sha256':installed}

def source_guard(source,adoption):
 plan=pins();assert adoption['status']=='ADOPTED_PRIVATE_I2P_ATTACHMENT_SOURCE_ONLY' and adoption['basis_commit']==BASIS and adoption['source_commit']==source
 assert adoption['source_plan']==ref(W/'source-plan.json') and adoption['source_paths']==plan['source_paths'] and len(plan['source_paths'])==31
 assert git('show','-s','--format=%P',source)==BASIS and set(git('diff','--name-only',BASIS,source).splitlines())==set(plan['source_paths'])
 for name,row in plan['source_paths'].items():assert sha(N/name)==row['after_sha256'],name
 return snapshot(source)

def main():
 parser=argparse.ArgumentParser();parser.parse_args();plan=pins();assert git('rev-parse','HEAD')==BASIS and git('branch','--show-current')=='main' and not git('status','--porcelain')
 assert protected()==plan['original_head_index_unchanged'] and (R/'migration/upstreams.lock.json').read_bytes()==(N/'migration/upstreams.lock.json').read_bytes()
 before=snapshot(BASIS);oldshipping=shipping();assert not (W/'root-adoption.json').exists()
 for name,row in plan['source_paths'].items():assert (sha(N/name) if (N/name).exists() else None)==row['before_sha256'],name
 for name,row in plan['source_paths'].items():
  target=N/name;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(pathlib.Path(row['after_path']).read_bytes())
 subprocess.run(['git','-C',str(N),'add','--',*plan['source_paths']],check=True);subprocess.run(['git','-C',str(N),'diff','--cached','--check'],check=True)
 subprocess.run(['git','-C',str(N),'-c','user.name=Codex','-c','user.email=codex@localhost','-c','commit.gpgsign=false','commit','-q','-m','Attach an explicit private I2P provider owner'],check=True)
 source=git('rev-parse','HEAD');adoption={'status':'ADOPTED_PRIVATE_I2P_ATTACHMENT_SOURCE_ONLY','basis_commit':BASIS,'source_commit':source,'source_plan':ref(W/'source-plan.json'),'source_paths':plan['source_paths'],'source_capsules':[plan[x] for x in ('production_manifest','acceptance_manifest','provider_source_manifest')],'original_head_index_unchanged':protected(),'shipping_provider_unchanged':oldshipping,'runtime_credit':False,'default_or_public_activation':False,'stop_after_this_section':True}
 current=source_guard(source,adoption);expected=dict(before['files_sha256']);expected.update({n:r['after_sha256'] for n,r in plan['source_paths'].items()});assert current['files_sha256']==expected and shipping()==oldshipping and protected()==plan['original_head_index_unchanged']
 write(W/'root-adoption.json',adoption);write(W/'adopted-source.json',current);print('ADOPTED_SOURCE_ONLY',source)
if __name__=='__main__':main()
