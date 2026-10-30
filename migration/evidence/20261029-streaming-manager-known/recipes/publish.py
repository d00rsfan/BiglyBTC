#!/usr/bin/env python3
"""Root-only metadata publication after actual qualification/promotion."""
import importlib.util,json,pathlib,shutil,subprocess
R=pathlib.Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';W=pathlib.Path(__file__).resolve().parent;E=pathlib.Path('migration/evidence/20261029-streaming-manager-known')
spec=importlib.util.spec_from_file_location('associated_closeout',W/'finish.py');f=importlib.util.module_from_spec(spec);spec.loader.exec_module(f);b=f.b
def read(p):return json.loads(pathlib.Path(p).read_bytes())
def sha(p):return b.s.sha(p)
def git(*args,**kw):return subprocess.run(['git','-C',str(N),*args],check=True,**kw)
def main():
 assert not (W/'publish.json').exists();protected=b.protected_original();promotion=read(W/'promotion.json');v=read(R/E/'verification.json');source=promotion['source_commit'];f.recipe_pins()
 assert v['status']=='PASS_STREAMING_MANAGER_KNOWN_ASSOCIATED_ROWS' and v['source_commit']==source and v['promotion']==promotion and v['source_paths']==25
 assert subprocess.check_output(['git','-C',str(N),'rev-parse','HEAD'],text=True).strip()==source and subprocess.check_output(['git','-C',str(N),'branch','--show-current'],text=True).strip()=='main'
 assert not subprocess.check_output(['git','-C',str(N),'diff','--cached','--name-only'],text=True).strip() and set(subprocess.check_output(['git','-C',str(N),'diff','--name-only'],text=True).splitlines())<={'migration/upstreams.lock.json'}
 for name,pin in v['artifacts_sha256'].items():assert sha(R/E/name)==pin
 assert (R/E/'verification.json').read_bytes()==(N/E/'verification.json').read_bytes() and sha(N/'dist/BUILD_MANIFEST.json')==promotion['manifest_sha256']
 dist=read(N/'dist/BUILD_MANIFEST.json');assert {str(p.relative_to(N/'dist')):sha(p) for p in (N/'dist').rglob('*') if p.is_file() and p.name!='BUILD_MANIFEST.json'}==dist['files_sha256']
 assert promotion['previous_dist_removed'] and promotion['atomic_exchange'] and promotion['both_registry_preflights_passed'] and promotion['test_support_removed'] and promotion['namespace_wrapper_and_launcher_removed'] and promotion['copied_test_images_removed']==6
 assert v['helper_preparation']['qualification_javac_invocations']==0 and v['helper_preparation']['actual_preparation_javac_invocations']==1 and v['helper_preparation']['prepared_classes']==4
 assert v['helper_preparation']['actual_fixture_preparation']==f.a1.ref(f.a1.REFERENCE) and v['helper_preparation']['fixture']==f.a1.ref(f.a1.F/'manifest.json') and v['helper_preparation']['actual_callback_bytecode_proof']==f.a1.ref(f.a1.D/'fin-callback-order-proof.json')
 assert v['failed_current_attempt']==b.failed_origin_pins() and v['earlier_failures_preserved'] and v['stop_after_current_milestone'] and v['fresh_full_cohort'] is False
 assert v['private_suites']==v['private_unique_test_images']==6 and v['gcc_suites']==22 and v['common_suites']==19 and v['fresh_suites_per_lane']==3 and v['retained_exact_r2_suites']=={'gcc':18,'clang':16,'san':16}
 assert all(row['associated_suites']==(22 if lane=='gcc' else 19) and row['fresh_actual_suites']==3 and row['fresh_full_cohort'] is False and row['retained_exact_r2_suites']==(18 if lane=='gcc' else 16) and row['actual_manager_applications']==4 for lane,row in v['lanes'].items()) and set(v['lanes'])=={'gcc','clang','san'}
 for label,row in v['registry_validation'].items():assert row['exit']==0 and sha(R/E/('registry-validation-'+label+'.log'))==row['log_sha256']
 assert set(v['registry_validation'])=={R.name,N.name};lock=(R/'migration/upstreams.lock.json').read_bytes();assert lock==(N/'migration/upstreams.lock.json').read_bytes()==(R/E/'proposed-upstreams.lock.json').read_bytes()
 data=json.loads(lock);assert len(data['upstreams'])==20 and len(data['components'])==108 and len(data['external_dependencies'])==22
 assert all(next(c for c in data['components'] if c['id']==name)['state']=='in_progress' for name in ('services.i2cp_initial_session','transport.azureus_volatile_database'))
 review='[Review](evidence/20261029-streaming-manager-known/REVIEW.md).'
 summary=f'Source `{source}` qualifies explicit Known endpoint attachment and the bounded four-stream I2P manager through exact-source/image associations: GCC22, Clang19 and leak-enabled ASan+UBSan19 selected rows. Each lane freshly runs all three affected suites (seven original-manager controls plus the four-application manager);16 unchanged rows per common lane and18 for GCC retain their original exact-image/definition origins, and GCC Public has a separate fresh authenticated result. These are associated rows, not a fresh full-cohort invocation. '+review
 detail='Six focused private subjects use six exact current GCC ELFs and per-PID private Qt/provider initialization proof: five initial subjects plus a later authenticated Public continuation with the same immutable candidate. They cover owned manager, actual Java manager, Known hints, isolated Public hint/export, twelve-frame source pressure, and retained legacy HTTP. The actual four-application manager includes two concurrent HTTP streams, inbound BT-shaped handshake/keepalives, late cancelled peer bytes and genuine renewal across at least125s. The source-backed Java fixture EOF/close callback fence and measured JVM-startup allowance preserve real ACK and byte checks; earlier failed runs remain failures. Broader intermittent SYN causality, stability and performance parity remain unestablished. The b086 modified JAR remains distinct from official source.'
 gaps='Further implementation is paused at the user’s request after this milestone. Whole plan remains incomplete. Public/default/Host and real remote tunnel activation, contact churn/alias replacement, protocol beyond51..55, broader tracker/backend integration and resource/performance parity remain open. Known import remains unseeded until actual self integration; supplied hints never become observed/persisted endpoint protocol facts. No SDK or chat milestone is advanced.'
 (R/'migration/STATE.md').write_text('# Migration state\n\nRead [ACTIVE.md](ACTIVE.md) for authoritative continuation.\n\n'+summary+'\n\n'+detail+f'\n\nOne current dist: `{promotion["manifest_sha256"]}`, {promotion["leaf_count"]} leaves. Previous preview and copied tests were removed after measured atomic promotion; both registry preflights passed.\n\n'+gaps+'\n')
 p=R/'migration/ACTIVE.md';text=p.read_text();at=text.find('\n## ');at=len(text) if at<0 else at+1;p.write_text(text[:at].rstrip()+'\n\n## Latest accepted milestone — Streaming manager and Known attachment\n\n'+summary+'\n\n'+detail+'\n\n'+gaps+' Earlier entries retain their own historical source/qualification scope.\n\n'+text[at:])
 names=['MIGRATION_PLAN.md','migration/tasks/P07-02b-i2cp-initial-session.md','migration/tasks/P06-AZUREUS-VOLATILE-DATABASE.md']
 for name in names:
  p=R/name;p.write_text(p.read_text().rstrip()+'\n\n## 2026-10-30 bounded manager and explicit Known attachment\n\n'+summary+'\n\n'+detail+'\n\n'+gaps+'\n')
 p=R/'migration/FEATURE_MATRIX.md';lines=p.read_text().splitlines();seen=set()
 for i,line in enumerate(lines):
  if line.startswith('| F06.i2cp_initial_session |'):
   seen.add('manager');lines[i]=f'| F06.i2cp_initial_session | Signed I2CP, datagrams, tracker and bounded Streaming manager | [Session](tasks/P07-02b-i2cp-initial-session.md) | In progress; `{source}` has actual four-application manager acceptance across125s/renewal/cancel on all three lanes and focused private Qt, alongside retained seven original-manager controls. Full Host/backend/tunnel/performance parity remains open. {review} |'
  if line.startswith('| F09.db_mapping |'):
   seen.add('known');lines[i]=f'| F09.db_mapping | Original mapping and bounded volatile runtime | [Runtime](tasks/P06-AZUREUS-VOLATILE-DATABASE.md) | In progress; `{source}` binds explicit Known endpoints before NodeAdded, validates actual PING protocol/Alive without seeding, and qualifies source/deadline/alias/capacity controls including isolated Public hint exclusion. Default/Host/Internet and broader parity remain open. {review} |'
 assert seen=={'manager','known'};p.write_text('\n'.join(lines)+'\n');names+=['migration/ACTIVE.md','migration/STATE.md','migration/FEATURE_MATRIX.md','migration/upstreams.lock.json','migration/reports/20261030-i2p-fin-close-race.txt','migration/reports/20261028-empty-v2-hybrid-bug.txt','migration/M-CURRENT-HANDOFF.md','migration/tools/continuation_status.py']
 for name in names:shutil.copyfile(R/name,N/name)
 assert b.protected_original()==protected and (R/'migration/upstreams.lock.json').read_bytes()==(N/'migration/upstreams.lock.json').read_bytes()
 # Raw LD_DEBUG output has PID suffixes, not .log; preserve exact evidence.
 raw_excludes=[]
 for name,expected in v['artifacts_sha256'].items():
  item=pathlib.Path(name);stem,sep,pid=item.name.rpartition('.')
  if stem.startswith('native-loader-') and sep and pid.isdigit():
   assert not item.is_absolute() and '..' not in item.parts
   relative=E/item;assert b.s.sha(R/relative)==b.s.sha(N/relative)==expected
   raw_excludes.append(':(exclude)'+str(relative))
 git('add','--',*names,str(E));excludes=[':(exclude)**/*.'+x for x in ('log','xml','patch','txt','frames')]+[':(exclude)**/original-source/**']+raw_excludes
 with (W/'diff-check.log').open('wb') as output:git('diff','--cached','--check','--','.',*excludes,stdout=output,stderr=subprocess.STDOUT)
 git('-c','user.name=Codex','-c','user.email=codex@localhost','-c','commit.gpgsign=false','commit','-q','-m','Qualify bounded Streaming manager and Known peer attachment')
 commit=subprocess.check_output(['git','-C',str(N),'rev-parse','HEAD'],text=True).strip()
 with (W/'push.log').open('wb') as output:git('push','origin','main',stdout=output,stderr=subprocess.STDOUT)
 assert not subprocess.check_output(['git','-C',str(N),'status','--porcelain'],text=True).strip() and b.protected_original()==protected
 (W/'publish.json').write_text(json.dumps({'commit':commit,'branch':'main','pushed':True,'source_commit':source,'original_head_index_unchanged':protected},indent=2)+'\n');print('COMMITTED_PUSHED_CLEAN',commit)
if __name__=='__main__':main()
