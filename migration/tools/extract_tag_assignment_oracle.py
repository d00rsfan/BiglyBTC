# SPDX-License-Identifier: GPL-2.0-or-later
"""Extract the pinned original seven lifecycle assignment action branches.
The surrounding scheduler/download interfaces are explicitly recording boundaries.
"""
from pathlib import Path
import subprocess,hashlib,json,sys
baseline='cb8c6cb9bb97916cbc7e80338a017200ffc3e71f'
source_path='core/src/com/biglybt/core/tag/impl/TagDownloadWithState.java'
repo=Path(sys.argv[1]);out=Path(sys.argv[2]);out.mkdir(parents=True,exist_ok=True)
source=subprocess.check_output(['git','-C',str(repo),'show',baseline+':'+source_path]).decode()
start='if ( \tisActionEnabled( TagFeatureExecOnAssign.ACTION_START ) ||'
end='if ( isActionEnabled( TagFeatureExecOnAssign.ACTION_POST_MAGNET_URI ))'
assert source.count(start)==source.count(end)==1
body=source[source.index(start):source.index(end)]
header='''// SPDX-License-Identifier: GPL-2.0-or-later
package com.biglybt.core.tag.impl;
import com.biglybt.core.download.DownloadManager;
import com.biglybt.core.tag.TagFeatureExecOnAssign;
import com.biglybt.core.util.*;
import java.lang.reflect.*;
import java.nio.file.*;
import java.util.*;
/** Exact pinned lifecycle-action branch; recording async and DM boundaries.
 * Not the whole addTaggable method, live async dispatcher or profile owner. */
public final class OriginalAssignmentLifecycleActions {
 int enabled;List<Object> trace=new ArrayList<>();
 static final class Queue {List<AERunnable> tasks=new ArrayList<>();void dispatch(AERunnable task){tasks.add(task);}}
 Queue rs_async=new Queue();
 boolean isActionEnabled(int action){return (enabled&action)!=0;}
 void checkMaximumTaggables(){trace.add("check-max");}
 void assign(DownloadManager dm,boolean metadata,boolean destroyed){
  if(!destroyed&&!metadata){
'''
footer='''
  }
 }
 static int mask(int bits){int[] flags={2,4,8,16,64,128,32768};int result=0;for(int i=0;i<flags.length;i++)if((bits&(1<<i))!=0)result|=flags[i];return result;}
 static Object empty(Class<?> type){if(type==boolean.class)return false;if(type==int.class)return 0;if(type==long.class)return 0L;return null;}
 static Map<String,Object> run(int enabled,int state,boolean metadata,boolean destroyed,int execution){
  OriginalAssignmentLifecycleActions original=new OriginalAssignmentLifecycleActions();original.enabled=enabled;
  DownloadManager dm=(DownloadManager)Proxy.newProxyInstance(DownloadManager.class.getClassLoader(),new Class[]{DownloadManager.class},(p,m,a)->{
   switch(m.getName()){
   case "getState":return state;
   case "setStateQueued":original.trace.add("queue");break;
   case "resume":original.trace.add("resume");break;
   case "stopIt":original.trace.add("stop:"+a[0]);break;
   case "pause":original.trace.add("pause:"+(Boolean.TRUE.equals(a[0])?1:0));break;
   case "setForceStart":original.trace.add("force:"+(Boolean.TRUE.equals(a[0])?1:0));break;
   default:break;
   }
   return empty(m.getReturnType());
  });
  original.assign(dm,metadata,destroyed);int queued=original.rs_async.tasks.size();original.enabled=execution;
  for(AERunnable task:original.rs_async.tasks)task.runSupport();
  Map<String,Object> row=new TreeMap<>();row.put("enabled",(long)enabled);row.put("execution",(long)execution);row.put("state",Integer.toString(state));row.put("metadata",metadata?1L:0L);row.put("destroyed",destroyed?1L:0L);row.put("queued",(long)queued);row.put("trace",original.trace);return row;
 }
 public static void main(String[] args)throws Exception{
  int[] states={Integer.MIN_VALUE,-1,0,30,50,55,60,65,70,71,75,100,Integer.MAX_VALUE};
  List<Object> rows=new ArrayList<>();for(int bits=0;bits<128;bits++)for(int state:states){int enabled=mask(bits);rows.add(run(enabled,state,false,false,enabled));}
  for(int state:states){rows.add(run(mask(127),state,true,false,mask(127)));rows.add(run(mask(127),state,false,true,mask(127)));}
  for(int enabled:new int[]{2,128,4,64,32768,2|8,4|16})for(int state:new int[]{50,70,100})for(int execution:new int[]{0,2,4,32768,64})rows.add(run(enabled,state,false,false,execution));
  Map<String,Object> result=new TreeMap<>();result.put("rows",rows);Files.write(Paths.get(args[0]),BEncoder.encode(result));
  System.out.println("ORIGINAL_ASSIGNMENT_LIFECYCLE rows="+rows.size()+" exact_pinned_branch=1 recording_async_dm_boundaries=1 PASS");
  com.biglybt.core.security.SESecurityManager.exitVM(0);
 }
}
'''
licence=source[:source.index('package ')].strip()
(out/'OriginalAssignmentLifecycleActions.java').write_text(licence+'\n'+header+body+footer)
(out/'source.json').write_text(json.dumps({'baseline_commit':baseline,'source_path':source_path,'source_sha256':hashlib.sha256(source.encode()).hexdigest(),'exact_branch_sha256':hashlib.sha256(body.encode()).hexdigest(),'scope':'seven lifecycle actions only;recording async/download boundaries;outer destroyed/metadata guards supplied'},indent=2)+'\n')
print('extracted pinned lifecycle branch',len(body),'bytes')
