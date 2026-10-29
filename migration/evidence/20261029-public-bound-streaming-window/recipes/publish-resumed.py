#!/usr/bin/env python3
"""Root-only metadata publication after actual window qualification/promotion."""
import hashlib,json,pathlib,shutil,subprocess
R=pathlib.Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';W=pathlib.Path(__file__).resolve().parent;E=pathlib.Path('migration/evidence/20261029-public-bound-streaming-window')
def read(p):return json.loads(pathlib.Path(p).read_bytes())
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def git(*args,**kw):return subprocess.run(['git','-C',str(N),*args],check=True,**kw)
def main():
 assert not (W/'publish.json').exists();promotion=read(W/'promotion.json');v=read(R/E/'verification.json');source=promotion['source_commit']
 assert v['status']=='PASS_RESUMED_PUBLIC_BOUND_STREAMING_WINDOW' and v['qualification_mode']=='resumed_same_source_current_cohort' and v['original_failed_invocations_preserved'] is True and v['source_commit']==source and v['promotion']==promotion
 assert subprocess.check_output(['git','-C',str(N),'rev-parse','HEAD'],text=True).strip()==source
 assert subprocess.check_output(['git','-C',str(N),'branch','--show-current'],text=True).strip()=='main'
 assert not subprocess.check_output(['git','-C',str(N),'diff','--cached','--name-only'],text=True).strip()
 assert set(subprocess.check_output(['git','-C',str(N),'diff','--name-only'],text=True).splitlines())<={'migration/upstreams.lock.json'}
 for name,pin in v['artifacts_sha256'].items():assert sha(R/E/name)==pin
 assert (R/E/'verification.json').read_bytes()==(N/E/'verification.json').read_bytes() and sha(N/'dist/BUILD_MANIFEST.json')==promotion['manifest_sha256']
 dist=read(N/'dist/BUILD_MANIFEST.json');assert {str(p.relative_to(N/'dist')):sha(p) for p in (N/'dist').rglob('*') if p.is_file() and p.name!='BUILD_MANIFEST.json'}==dist['files_sha256']
 assert promotion['previous_dist_removed'] and promotion['atomic_exchange'] and promotion['both_registry_preflights_passed'] and promotion['test_support_removed']
 assert promotion['copied_test_images_removed']==v['private_unique_test_images'] and v['private_suites']==v['gcc_suites'] and all(x['actual_suites']==(v['gcc_suites'] if lane=='gcc' else v['common_suites']) for lane,x in v['lanes'].items())
 for label,row in v['registry_validation'].items():assert row['exit']==0 and sha(R/E/('registry-validation-'+label+'.log'))==row['log_sha256']
 assert set(v['registry_validation'])=={R.name,N.name}
 lock=(R/'migration/upstreams.lock.json').read_bytes();assert lock==(N/'migration/upstreams.lock.json').read_bytes()==(R/E/'proposed-upstreams.lock.json').read_bytes()
 assert next(c for c in json.loads(lock)['components'] if c['id']=='services.i2cp_initial_session')['state']=='in_progress'
 review='[Review](evidence/20261029-public-bound-streaming-window/REVIEW.md).'
 summary=f'Source `{source}` closes the resumed same-source current cohort: Clang passed its original complete16-suite invocation; GCC18 and SAN15 unaffected actual rows are retained from their original failed invocations, with only each pre-subject authorization-blocked Public suite rerun. Original failures remain linked; no full fresh rerun is claimed. This qualifies the explicit bounded native Streaming window: the associated actual GCC/Clang/leak-enabled ASan+UBSan result covers {v["gcc_suites"]}/{v["common_suites"]}/{v["common_suites"]} selected suites, including all five retained modified-manager controls and cap2/cap8 128KiB upload/digest/FIN/EOF cases. {v["private_suites"]} private commands use {v["private_unique_test_images"]} exact GCC test images against private Qt. PublicBound executes through the exact copied enforcing network/user-namespace wrapper. '+review
 measurements=v['private_upload_measurements'];rates=', '.join(f'{name}: {row["body_bytes_per_second"]} body bytes/s, {row["elapsed_microseconds"]}us, peak flight {row["actual_maximum_in_flight"]}, retries {row["retransmissions"]}' for name,row in sorted(measurements.items()))
 detail='Actual controlled-route private observations (same40ms actual peer-frame delay): '+rates+'. These are finite fixture measurements, not a guaranteed speedup or production performance-parity claim.'
 gaps='Multi-stream/listener/backend integration, public/default/Host activation, anonymous remote tunnels and broader performance/resource parity remain open. No old Java-object/router/provider pass was pooled into this window result.'
 (R/'migration/STATE.md').write_text('# Migration state\n\nRead [ACTIVE.md](ACTIVE.md) for authoritative continuation. The whole migration plan remains authorised.\n\n'+summary+'\n\n'+detail+f'\n\nOne current dist: `{promotion["manifest_sha256"]}`, {promotion["leaf_count"]} leaves. Both proposed registry validations passed before atomic exchange; predecessor, {v["private_unique_test_images"]} copied tests and QtTest support were removed after hashing.\n\n'+gaps+'\n')
 p=R/'migration/ACTIVE.md';text=p.read_text();at=text.find('\n## ');at=len(text) if at<0 else at+1;text=text[:at].rstrip()+'\n\n## Latest accepted milestone — PublicBound and bounded Streaming window\n\n'+summary+'\n\n'+detail+'\n\n'+gaps+' Continue the whole plan; older notes below preserve history.\n\n'+text[at:];p.write_text(text)
 names=['MIGRATION_PLAN.md','migration/tasks/P07-02b-i2cp-initial-session.md','migration/tasks/P06-AZUREUS-VOLATILE-DATABASE.md']
 for name in names:
  p=R/name;p.write_text(p.read_text().rstrip()+'\n\n## 2026-10-29 PublicBound and bounded Streaming window\n\n'+summary+'\n\n'+detail+'\n\n'+gaps+'\n')
 p=R/'migration/FEATURE_MATRIX.md';lines=p.read_text().splitlines();found=False
 for index,line in enumerate(lines):
  if line.startswith('| F06.i2cp_initial_session |'):
   assert not found;found=True;lines[index]=f'| F06.i2cp_initial_session | Signed I2CP, datagrams, tracker, Enc4 and bounded Streaming window | [Session](tasks/P07-02b-i2cp-initial-session.md) | In progress; `{source}` qualifies five retained actual-manager controls plus real 128KiB upload/digest and cap2/cap8 finite flight, ACK, bounds and timing observations on all three lanes/private Qt. Whole backend, listener/multiplexing, public/Host and resource/performance parity remain open. {review} |'
 assert found
 found=False
 for index,line in enumerate(lines):
  if line.startswith('| F09.db_mapping |'):
   assert not found;found=True;lines[index]=f'| F09.db_mapping | Original mapping and bounded volatile runtime | [Runtime](tasks/P06-AZUREUS-VOLATILE-DATABASE.md) | In progress; `{source}` qualifies explicit direct/filter-disabled PublicBound policy through actual enforcing IPv4/IPv6 namespaces, plus affected routing/control/owner/profile IO/maintenance/database/byte-accounting regressions. Default/Host/Internet activation, filtering and broader parity remain open. {review} |'
 assert found;p.write_text('\n'.join(lines)+'\n')
 lockdata=json.loads(lock);db=next(c for c in lockdata['components'] if c['id']=='transport.azureus_volatile_database');assert db['state']=='in_progress'
 p=R/'MIGRATION_PLAN.md';p.write_text(p.read_text().rstrip()+'\n\nExplicit PublicBound direct/filter-disabled policy now has actual owned namespace IPv4/IPv6 acceptance and affected regression coverage. Default/Host/Internet DHT activation and broader filtering/parity remain open. '+review+'\n')
 names+=['migration/ACTIVE.md','migration/STATE.md','migration/FEATURE_MATRIX.md','migration/upstreams.lock.json']
 for name in names:shutil.copyfile(R/name,N/name)
 git('add','--',*names,str(E));excludes=[':(exclude)**/*.'+x for x in ('log','xml','patch','txt','frames')]+[':(exclude)**/original-source/**']
 with (W/'diff-check.log').open('wb') as output:git('diff','--cached','--check','--','.',*excludes,stdout=output,stderr=subprocess.STDOUT)
 git('-c','user.name=Codex','-c','user.email=codex@localhost','-c','commit.gpgsign=false','commit','-q','-m','Qualify explicit PublicBound and bounded Streaming window')
 commit=subprocess.check_output(['git','-C',str(N),'rev-parse','HEAD'],text=True).strip()
 with (W/'push.log').open('wb') as output:git('push','origin','main',stdout=output,stderr=subprocess.STDOUT)
 assert not subprocess.check_output(['git','-C',str(N),'status','--porcelain'],text=True).strip();(W/'publish.json').write_text(json.dumps({'commit':commit,'branch':'main','pushed':True,'source_commit':source},indent=2)+'\n');print('COMMITTED_PUSHED_CLEAN',commit)
if __name__=='__main__':main()
