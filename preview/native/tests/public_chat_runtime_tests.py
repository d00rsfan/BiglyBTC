# SPDX-License-Identifier: GPL-2.0-or-later
"""Generated temporary SDK copies are removed after positive/negative checks."""
import argparse,os,resource,shutil,subprocess,tempfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--driver',required=True);p.add_argument('--provider',type=Path,required=True);p.add_argument('--parent',type=Path,required=True);args=p.parse_args()
resource.setrlimit(resource.RLIMIT_CORE,(0,0));env=os.environ.copy();env.pop('LD_PRELOAD',None);env.pop('LD_AUDIT',None)
def run(label,root=None,reject=False,extra=None):
 e=env.copy()
 if root is not None:e['LD_LIBRARY_PATH']=str(root)
 if extra:e.update(extra)
 value=subprocess.run([args.driver]+(['reject'] if reject else []),env=e,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=20)
 if value.returncode:raise SystemExit(label+' failed: '+str(value.returncode)+'\n'+value.stdout.decode(errors='replace')[-1200:])
 print('PASS '+label)
run('exact Public shared-chat image',args.provider.parent)
run('old parent image refused',args.parent.parent,True)
run('loader audit environment refused',args.provider.parent,True,{'LD_AUDIT':'/nonexistent-owned-test-audit.so'})
with tempfile.TemporaryDirectory(prefix='biglybtc-chat-runtime-') as temporary:
 root=Path(temporary);library=root/args.provider.name;shutil.copyfile(args.provider,library);(root/'libtorrent-rasterbar.so.2.0').symlink_to(library.name)
 run('exact image relocates',root)
 with library.open('ab') as f:f.write(b'\0')
 run('changed relocated image refused',root,True)
