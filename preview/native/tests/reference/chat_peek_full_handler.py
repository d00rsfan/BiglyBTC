#!/usr/bin/env python3
"""Add an encrypted peek command to the opt-in generated full-handler fixture.
The original MsgSyncHandler itself is unchanged. Only the existing test driver
is extended; its fenced, in-memory DHT/host collaborators remain unchanged.
"""
from pathlib import Path
import argparse, subprocess, os
p=argparse.ArgumentParser();p.add_argument('--root',type=Path,required=True);p.add_argument('--java-home',type=Path,required=True);p.add_argument('--core',type=Path,required=True);a=p.parse_args()
f=a.root/'full-handler/source/com/vuze/plugins/azmsgsync/FullHandlerOracle.java'
s=f.read_text();anchor='   else if(type==9)'
if 'else if(type==12)' not in s:
 s=s.replace(anchor,'''   else if(type==12){
    Field peek=MsgSyncHandler.class.getDeclaredField("peek_xfer_handler");peek.setAccessible(true);
    DHTPluginTransferHandler transfer=(DHTPluginTransferHandler)peek.get(handler);
    byte[] reply=transfer.handleRead(remote,payload);frame(13,reply==null?new byte[0]:reply);
   }
'''+anchor)
 f.write_text(s)
env=os.environ.copy()
for k in ['JAVA_TOOL_OPTIONS','JDK_JAVA_OPTIONS','_JAVA_OPTIONS','CLASSPATH']:env.pop(k,None)
r=subprocess.run([str(a.java_home/'bin/javac'),'-J-Xmx192m','--release','8','-encoding','UTF-8','-cp',str(a.root/'full-handler/classes')+':'+str(a.core),'-d',str(a.root/'full-handler/classes'),str(f)],env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=60)
if r.returncode:raise SystemExit(r.stdout.decode(errors='replace')[-3000:])
