from pathlib import Path
import hashlib,json,re,shutil,subprocess
R=Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';D=Path(__file__).resolve().parent;W=R/'build/public-nat-20261114';EV='migration/evidence/20261121-profile-tag-preadd-cohort-log';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
source=json.loads((D/'source-publish.json').read_text());assert source['qualification']['qt_rows']==199
all_tasks=[]
for name in ['final-isolated-checks.json','final-abi1-checks.json','corrected-final-native-checks.json','final-current-checks.json','before-checks.json']:all_tasks+=json.loads((D/name).read_text())
names=[t['name'] for t in all_tasks];assert len(names)==len(set(names))==130
groups=[];qt=0
for task in all_tasks:
 name=task['name'];data=(W/(name+'.log')).read_text(errors='replace');assert not re.search(r'FAIL!|AddressSanitizer:|runtime error:',data),name
 totals=re.findall(r'Totals: (\d+) passed, (\d+) failed, (\d+) skipped',data)
 if totals:
  assert all(f=='0' and s=='0' for _,f,s in totals);passed=sum(int(p) for p,_,_ in totals);qt+=passed;row=dict(name=name,qt_passed=passed,failed=0,skipped=0)
 else:
  markers=[s for s in data.splitlines() if 'PASS' in s];assert markers,name;row=dict(name=name,accepted=markers[-1])
 row['log_sha256']=sha(W/(name+'.log'));groups.append(row)
assert qt==209
for name,h in source['qualification']['verification_inputs'].items():assert sha(N/name)==h,name
review='''# Profile pending census cohort log correction

Implementation remains `ad63fce04e5ba493c4510f139acffe167d1f6572`,tree
`4f7af432f3705bc69236d040a64fae0e4709663a`. No production/test source changed.
The prior evidence remains immutable at
`migration/evidence/20261121-profile-tag-preadd`.

Its Native spec used `profile-pending-native-cohort-final` for both the existing
Qt cohort executable and the new plain pending-cohort mode. Both ran exit0,
but the latter overwrote the former log. Thus the archived tally199 counted
that plain log twice and omitted10 Qt rows. This is an evidence identity bug.

Only the existing Qt cohort test was rerun under the unique
`profile-pending-native-existing-cohort-final` identity:10 passed,0 failed,
0 skipped. The corrected full spec has130 distinct task identities and209 Qt
rows. All other final logs retain their already qualified hashes. The original
199-row record is preserved;this new record supplies the corrected manifest.
Both the reusable runner and publisher now reject duplicate task identities
before execution/publication. Updated helpers and the exact rerun log are
retained here. SDK/daily/profile selection and all private/global limits stay
as recorded in the original bounded review. Continue private integration.
'''
qualification=dict(source['qualification'],groups=groups,runtime_groups=130,unique_task_identities=130,qt_rows=209,native_commit='ad63fce04e5ba493c4510f139acffe167d1f6572',native_tree='4f7af432f3705bc69236d040a64fae0e4709663a',correction_of='migration/evidence/20261121-profile-tag-preadd/qualification.json',production_source_changed=False)
files=[EV+'/REVIEW.md',EV+'/qualification.json',EV+'/corrected-final-native-checks.json',EV+'/cohort-log-correction-checks.json',EV+'/profile-pending-native-existing-cohort-final.log',EV+'/run_checks.py',EV+'/publish_migration_milestone.py',EV+'/prepare_cohort_correction.py']
for repo in [R,N]:
 out=repo/EV;out.mkdir(parents=True,exist_ok=True);(out/'REVIEW.md').write_text(review);(out/'qualification.json').write_text(json.dumps(qualification,indent=2)+'\n')
 for file in ['corrected-final-native-checks.json','cohort-log-correction-checks.json','prepare_cohort_correction.py']:shutil.copy2(D/file,out/file)
 shutil.copy2(W/'profile-pending-native-existing-cohort-final.log',out/'profile-pending-native-existing-cohort-final.log');shutil.copy2(W/'run_checks.py',out/'run_checks.py');shutil.copy2(R/'build/publish_migration_milestone.py',out/'publish_migration_milestone.py')
 for task in ['migration/tasks/P08-TAG-RATE-LIMITS.md','migration/STATE.md']:
  path=repo/task;append='Profile pending census qualification correction:130 unique runtime identities and209 Qt rows;one duplicate Native cohort log was rerun under a distinct name. Implementation `ad63fce04e5ba493c4510f139acffe167d1f6572` unchanged. [Correction]('+EV+'/REVIEW.md). Historical record retained;continue private source/storage/Host integration.'
  if append not in path.read_text():path.write_text(path.read_text()+'\n'+append+'\n')
registry=json.loads((R/'migration/upstreams.lock.json').read_text());component=next(c for c in registry['components'] if c['id']=='library.tag_rate_limits')
for field,values in [('review_records',[EV+'/REVIEW.md']),('evidence',[EV+'/REVIEW.md',EV+'/qualification.json'])]:
 for value in values:
  if value not in component[field]:component[field].append(value)
import jsonschema
schema=json.loads((R/'migration/upstreams.schema.json').read_text());jsonschema.Draft202012Validator({'$ref':'#/$defs/component','$defs':schema['$defs']}).validate(component)
for repo in [R,N]:(repo/'migration/upstreams.lock.json').write_text(json.dumps(registry,indent=2)+'\n')
files+=['migration/tasks/P08-TAG-RATE-LIMITS.md','migration/STATE.md','migration/upstreams.lock.json'];(D/'cohort-correction-files.json').write_text(json.dumps(files,indent=2)+'\n');print('Prepared evidence-only correction:',len(files),'files;130 distinct runtime identities;209 Qt rows;implementation unchanged')
