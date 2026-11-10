#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile source-pinned original room classes; never builds/launches the Java app."""
import argparse,hashlib,json,os,re,shutil,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--root',type=Path,required=True);p.add_argument('--core-classes',type=Path,required=True);p.add_argument('--plugin-source',type=Path,required=True);p.add_argument('--java-home',type=Path,required=True);a=p.parse_args()
here=Path(__file__).resolve().parent;pins=json.loads((here/'source-pins.json').read_bytes());core=a.core_classes.resolve(strict=True);repo=core.parents[2];root=a.root.absolute()
assert root.resolve()==root and root.is_relative_to(repo/'build') and root!=repo/'build' and '.biglybt' not in root.parts,'Use a generated directory inside the original checkout build tree'
root.mkdir(parents=True,exist_ok=True);marker=root/'.owned-chat-oracle';assert not marker.is_symlink();marker.write_text('BiglyBTC generated reference only\n')
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
assert sha(repo/'core/src/com/biglybt/plugin/net/buddy/BuddyPluginBeta.java')==pins['buddy_source_sha256'],'Original Buddy ordering source changed'
plugin=a.plugin_source.resolve(strict=True)/'com/vuze/plugins/azmsgsync';w=root/'full-handler';src=w/'source/com/vuze/plugins/azmsgsync';src.mkdir(parents=True,exist_ok=True);classes=w/'classes';classes.mkdir(exist_ok=True)
for name,expected in pins['plugin_sources_sha256'].items():
 original=plugin/name;assert sha(original)==expected,'Original room source changed: '+name;shutil.copyfile(original,src/name)
for name in ['MsgSyncPlugin.java','FullHandlerOracle.java','OriginalRoomOrder.java']:shutil.copyfile(here/name,src/name)
shutil.copyfile(here/'FullChatFence.java',w/'source/FullChatFence.java')
for name in ['home','tmp']:(root/name).mkdir(exist_ok=True)
env=os.environ.copy()
for key in ['JAVA_TOOL_OPTIONS','JDK_JAVA_OPTIONS','_JAVA_OPTIONS','CLASSPATH','LD_PRELOAD','LD_AUDIT']:env.pop(key,None)
version=subprocess.run([str(a.java_home/'bin/java'),'-version'],env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,check=True,text=True).stdout
assert re.search(r'version "17\.',version),'The fenced oracle requires JDK17'
sources=[w/'source/FullChatFence.java']+sorted(src.glob('*.java'))
with (w/'compile.log').open('w') as log:subprocess.run([str(a.java_home/'bin/javac'),'-J-Xmx192m','-J-XX:ActiveProcessorCount=1','--release','8','-encoding','UTF-8','-cp',str(core),'-d',str(classes)]+list(map(str,sources)),env=env,stdout=log,stderr=subprocess.STDOUT,timeout=90,check=True)
(root/'prepared-reference.json').write_text(json.dumps({'status':'SOURCE_PINNED_COMPILED','original_core':pins['original_core'],'original_azmsgsync':pins['original_azmsgsync'],'sources_sha256':{str(f.relative_to(w/'source')):sha(f) for f in sources}},indent=2)+'\n');print('Source-pinned fenced reference compiled')
