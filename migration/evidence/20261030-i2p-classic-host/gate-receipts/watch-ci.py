import pathlib,subprocess,json,time,datetime
T=pathlib.Path('/home/yurii/Java/BiglyBT/build/host-i2p-20261030');commit='e864927ab8548c3f0670d0229d79e6f403133704';start=time.monotonic();previous=None
while time.monotonic()-start<2900:
 p=subprocess.run(['gh','run','list','--repo','d00rsfan/BiglyBTC','--commit',commit,'--limit','4','--json','databaseId,name,status,conclusion,url'],capture_output=True,text=True,timeout=30)
 if p.returncode:print(json.dumps({'ci_poll_exit':p.returncode}),flush=True);time.sleep(55);continue
 runs=json.loads(p.stdout);state={'source_commit':commit,'runs':runs,'updated_utc':datetime.datetime.now(datetime.timezone.utc).isoformat()}
 progress=[]
 for r in runs:
  if r['status']=='in_progress':
   q=subprocess.run(['gh','run','view',str(r['databaseId']),'--repo','d00rsfan/BiglyBTC','--json','jobs'],capture_output=True,text=True,timeout=30)
   if not q.returncode:progress += [s['name'] for j in json.loads(q.stdout)['jobs'] for s in j['steps'] if s['status']=='in_progress']
 state['current_steps']=progress
 if len(runs)>=2 and all(r['status']=='completed' for r in runs):state['status']='PASS' if all(r['conclusion']=='success' for r in runs) else 'FAIL'
 else:state['status']='PENDING'
 (T/'hosted-ci.json').write_text(json.dumps(state,indent=2)+'\n')
 with (T/'hosted-ci-polls.jsonl').open('a') as f:f.write(json.dumps(state)+'\n')
 stamp=json.dumps({'status':state['status'],'steps':progress,'conclusions':{r['name']:r['conclusion'] for r in runs}})
 if stamp!=previous:print(stamp,flush=True);previous=stamp
 if state['status']!='PENDING':raise SystemExit(0 if state['status']=='PASS' else 1)
 time.sleep(55)
raise SystemExit('CI observation timed out; receipt remains honest pending')
