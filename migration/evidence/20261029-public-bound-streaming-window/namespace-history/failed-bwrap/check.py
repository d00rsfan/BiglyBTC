import json,os,pathlib,socket,subprocess
fields=dict(line.split(':',1) for line in pathlib.Path('/proc/self/status').read_text().splitlines() if ':' in line)
assert all(int(fields[k].strip(),16)==0 for k in ('CapEff','CapPrm','CapInh','CapAmb'))
assert int(fields['NoNewPrivs'])==1
assert {n for _,n in socket.if_nameindex()}=={'lo'}
for env,ns,prefix in [('BIGLYBTC_OWNED_PUBLIC_NET_FD','net','net:['),('BIGLYBTC_OWNED_PUBLIC_USER_FD','user','user:[')]:
 fd=int(os.environ[env]);assert os.readlink(f'/proc/self/fd/{fd}').startswith(prefix)
 a=os.fstat(fd);b=os.stat('/proc/self/ns/'+ns);assert (a.st_dev,a.st_ino)!=(b.st_dev,b.st_ino)
for family in ('-4','-6'):assert not subprocess.check_output(['ip',family,'route','show','default']).strip()
print(json.dumps({'status':'PASS_ENVIRONMENT_FENCE_ONLY','interfaces':['lo'],'active_capabilities':0,'no_new_privileges':True,'functional_test_credit':False}))
