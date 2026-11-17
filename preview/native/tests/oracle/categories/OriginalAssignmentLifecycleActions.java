/*
 * Created on Mar 22, 2013
 * Created by Paul Gardner
 *
 * Copyright 2013 Azureus Software, Inc.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */
// SPDX-License-Identifier: GPL-2.0-or-later
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
if ( 	isActionEnabled( TagFeatureExecOnAssign.ACTION_START ) ||
								isActionEnabled( TagFeatureExecOnAssign.ACTION_RESUME )){
	
							int	dm_state = dm.getState();
	
							if ( 	dm_state == DownloadManager.STATE_STOPPED ||
									dm_state == DownloadManager.STATE_ERROR ){
	
								rs_async.dispatch(
									new AERunnable()
									{
										@Override
										public void
										runSupport()
										{
											if ( isActionEnabled( TagFeatureExecOnAssign.ACTION_START )){
	
												dm.setStateQueued();
	
											}else{
	
												dm.resume();
											}
										}
									});
							}
	
						}else if ( 	isActionEnabled( TagFeatureExecOnAssign.ACTION_STOP ) ||
									isActionEnabled( TagFeatureExecOnAssign.ACTION_PAUSE ) ||
									isActionEnabled( TagFeatureExecOnAssign.ACTION_QUEUE )){
	
							int	dm_state = dm.getState();
	
							if ( 	dm_state != DownloadManager.STATE_STOPPED &&
									dm_state != DownloadManager.STATE_STOPPING &&
									dm_state != DownloadManager.STATE_ERROR ){
	
								rs_async.dispatch(
									new AERunnable()
									{
										@Override
										public void
										runSupport()
										{
											if ( isActionEnabled( TagFeatureExecOnAssign.ACTION_STOP )){
	
												dm.stopIt( DownloadManager.STATE_STOPPED, false, false );
	
											}else if ( isActionEnabled( TagFeatureExecOnAssign.ACTION_QUEUE )){
	
												dm.stopIt( DownloadManager.STATE_QUEUED, false, false );
	
											}else{
	
												dm.pause( true );
											}
	
											// recheck here in case it is an 'archive' action that requires
											// download to be stopped
	
											checkMaximumTaggables();
	
										}
									});
							}
						}
	
						if ( isActionEnabled( TagFeatureExecOnAssign.ACTION_FORCE_START )){
	
							rs_async.dispatch(
								new AERunnable()
								{
									@Override
									public void
									runSupport()
									{
										dm.setForceStart( true);
									}
								});
	
						}else if ( isActionEnabled( TagFeatureExecOnAssign.ACTION_NOT_FORCE_START )){
	
							rs_async.dispatch(
								new AERunnable()
								{
									@Override
									public void
									runSupport()
									{
										dm.setForceStart( false );
									}
								});
						}
	
						
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
