// SPDX-License-Identifier: GPL-2.0-or-later
package com.biglybt.core.global.impl;
import com.biglybt.core.*;
import com.biglybt.core.download.*;
import com.biglybt.core.tag.*;
import com.biglybt.core.tag.impl.*;
import com.biglybt.core.util.*;
import java.lang.reflect.*;
import java.nio.file.*;
import java.util.*;
import sun.misc.Unsafe;
/** Actual original state/activity bodies with recording tag boundaries only. */
public final class OriginalDownloadStateTags {
 static final Unsafe U;static final Class<?> TYPE;
 static {try{Field f=Unsafe.class.getDeclaredField("theUnsafe");f.setAccessible(true);U=(Unsafe)f.get(null);TagManagerImpl manager=(TagManagerImpl)U.allocateInstance(TagManagerImpl.class);Field singleton=TagManagerImpl.class.getDeclaredField("singleton");singleton.setAccessible(true);singleton.set(null,manager);TYPE=Class.forName("com.biglybt.core.global.impl.GlobalManagerImpl$DownloadStateTagger");}catch(Exception e){throw new RuntimeException(e);}}
 static final class RecordingTag extends TagDownloadWithState {
  Set<DownloadManager> values;int id;
  RecordingTag(){super(null,0,"generated",false,false,false,false,0);}
  @Override public int getTagID(){return id;}
  @Override public void addTaggable(Taggable value){values.add((DownloadManager)value);}
  @Override public void removeTaggable(Taggable value){values.remove(value);}
  @Override public boolean hasTaggable(Taggable value){return values.contains(value);}
  @Override public Set<DownloadManager> getTaggedDownloads(){return values;}
 }
 static Object defaults(Method method){Class<?> type=method.getReturnType();if(type==boolean.class)return false;if(type==int.class)return 0;if(type==long.class)return 0L;return null;}
 static <T>T proxy(Class<T> type,InvocationHandler handler){return type.cast(Proxy.newProxyInstance(type.getClassLoader(),new Class[]{type},handler));}
 static void set(Object o,String name,Object value)throws Exception{Field f=TYPE.getDeclaredField(name);f.setAccessible(true);f.set(o,value);}
 static void call(Object o,String name,Class<?>[] signature,Object... args)throws Exception{Method m=TYPE.getDeclaredMethod(name,signature);m.setAccessible(true);m.invoke(o,args);}
 static final class Subject {
  int state;boolean complete,paused,destroyed;long received,sent;long receiveRate,sendRate;boolean activeWrite;
  final Map<Object,Object> data=new HashMap<>();final DownloadManager manager;
  Subject(){DownloadManagerState attributes=proxy(DownloadManagerState.class,(p,m,a)->{if(m.getName().equals("setLongAttribute")){activeWrite=true;return null;}return defaults(m);});DownloadManagerStats stats=proxy(DownloadManagerStats.class,(p,m,a)->{switch(m.getName()){case "getTotalDataBytesReceived":return received;case "getTotalDataBytesSent":return sent;case "getDataReceiveRate":return receiveRate;case "getDataSendRate":return sendRate;default:return defaults(m);}});manager=proxy(DownloadManager.class,(p,m,a)->{switch(m.getName()){case "isDestroyed":return destroyed;case "isDownloadComplete":return complete;case "isPaused":return paused;case "getUserData":return data.get(a[0]);case "setUserData":data.put(a[0],a[1]);return null;case "getStats":return stats;case "getDownloadState":return attributes;case "getState":return state;case "hashCode":return System.identityHashCode(p);case "equals":return p==a[0];default:return defaults(m);}});}
 }
 static void operation(Object tagger,Subject subject,int type,boolean added,boolean present)throws Exception{
  Class<?> listenerType=Class.forName(TYPE.getName()+"$2");Object listener=U.allocateInstance(listenerType);
  for(Field f:listenerType.getDeclaredFields())if(f.getType()==TYPE){f.setAccessible(true);f.set(listener,tagger);}
  CoreOperationTask task=proxy(CoreOperationTask.class,(p,m,a)->m.getName().equals("getDownload")?(present?subject.manager:null):defaults(m));
  CoreOperation op=proxy(CoreOperation.class,(p,m,a)->m.getName().equals("getOperationType")?type:m.getName().equals("getTask")?task:defaults(m));
  Method process=listenerType.getDeclaredMethod("process",CoreOperation.class,boolean.class);process.setAccessible(true);process.invoke(listener,op,added);
 }
 static Map<String,Object> step(String action,long... values){Map<String,Object> step=new TreeMap<>();step.put("action",action);List<Object> args=new ArrayList<>();for(long n:values)args.add(n);step.put("args",args);return step;}
 @SuppressWarnings("unchecked") static Map<String,Object> run(String name,List<Map<String,Object>> steps)throws Exception{
  Object tagger=U.allocateInstance(TYPE);Object mainKey=new Object(),completeKey=new Object();set(tagger,"main_tag_key",mainKey);set(tagger,"comp_tag_key",completeKey);String[] fields={"tag_initialising","tag_downloading","tag_seeding","tag_queued_downloading","tag_queued_seeding","tag_stopped","tag_error","tag_active","tag_paused","tag_inactive","tag_complete","tag_incomplete","tag_moving","tag_checking","tag_deleting"};RecordingTag[] tags=new RecordingTag[15];for(int i=0;i<15;i++){RecordingTag tag=(RecordingTag)U.allocateInstance(RecordingTag.class);tag.id=i;tag.values=Collections.newSetFromMap(new IdentityHashMap<DownloadManager,Boolean>());tags[i]=tag;set(tagger,fields[i],tag);}set(tagger,"derived_tags",new TagDownloadWithState[]{tags[7],tags[9],tags[10],tags[11],tags[12],tags[13],tags[14],tags[8]});Subject subject=new Subject();List<Object> result=new ArrayList<>();Field activeKey=GlobalManagerImpl.class.getDeclaredField("ACTIVE_KEY");activeKey.setAccessible(true);Object active=activeKey.get(null);
  for(Map<String,Object> input:steps){String action=(String)input.get("action");List<Object> args=(List<Object>)input.get("args");subject.activeWrite=false;if(action.equals("state")){subject.state=((Number)args.get(0)).intValue();subject.complete=((Number)args.get(1)).longValue()!=0;subject.paused=((Number)args.get(2)).longValue()!=0;subject.destroyed=((Number)args.get(3)).longValue()!=0;call(tagger,"stateChanged",new Class[]{DownloadManager.class,int.class},subject.manager,subject.state);}else if(action.equals("activity")){subject.received=((Number)args.get(0)).longValue();subject.sent=((Number)args.get(1)).longValue();subject.receiveRate=((Number)args.get(2)).longValue();subject.sendRate=((Number)args.get(3)).longValue();subject.destroyed=((Number)args.get(4)).longValue()!=0;call(tagger,"updateActive",new Class[]{});}else if(action.equals("operation")){operation(tagger,subject,((Number)args.get(0)).intValue(),((Number)args.get(1)).longValue()!=0,((Number)args.get(2)).longValue()!=0);}else if(action.equals("delete")){call(tagger,"removeInitiated",new Class[]{DownloadManager.class},subject.manager);}else if(action.equals("remove")){call(tagger,"remove",new Class[]{DownloadManager.class},subject.manager);}
   Map<String,Object> row=new TreeMap<>(input);List<Object> members=new ArrayList<>();for(RecordingTag tag:tags)members.add(tag.values.contains(subject.manager)?1L:0L);row.put("members",members);Tag main=(Tag)subject.data.get(mainKey);row.put("main",main==null?-1L:(long)main.getTagID());Boolean complete=(Boolean)subject.data.get(completeKey);row.put("complete",complete==null?-1L:complete?1L:0L);Long prefix=(Long)subject.data.get(active);row.put("active_prefix_known",prefix==null?0L:1L);row.put("active_prefix",prefix==null?0L:prefix);row.put("active_write",subject.activeWrite?1L:0L);result.add(row);
  }Map<String,Object> out=new TreeMap<>();out.put("name",name);out.put("steps",result);return out;
 }
 public static void main(String[] args)throws Exception{List<Object> cases=new ArrayList<>();int count=0;for(int state:new int[]{-1,0,5,10,20,30,40,50,55,60,65,70,71,75,100,999})for(int complete=0;complete<2;complete++)for(int paused=0;paused<2;paused++){cases.add(run("initial-"+state+"-"+complete+"-"+paused,Arrays.asList(step("state",state,complete,paused,0))));count++;}
  for(int before:new int[]{0,50,60,70,75,100})for(int wasComplete=0;wasComplete<2;wasComplete++)for(int after:new int[]{-1,0,5,10,20,30,40,50,55,60,65,70,71,75,100,999}){
   cases.add(run("transition-"+before+"-"+wasComplete+"-"+after,Arrays.asList(step("state",before,wasComplete,1,0),step("state",after,1-wasComplete,0,0))));count+=2;
  }
  List<Map<String,Object>> transitions=Arrays.asList(step("state",50,0,0,0),step("state",30,0,0,0),step("state",70,0,0,0),step("state",30,0,0,0),step("state",60,1,0,0),step("state",55,1,0,0),step("state",75,1,0,0),step("state",30,1,0,0),step("state",70,1,1,0),step("state",70,1,0,0),step("state",100,1,0,0),step("delete"),step("state",100,1,0,1));cases.add(run("transient-history-pause-complete-destroy",transitions));count+=transitions.size();
  List<Map<String,Object>> activity=Arrays.asList(step("state",50,0,0,0),step("activity",0,0,0,0,0),step("activity",100,0,0,0,0),step("activity",100,0,0,0,0),step("activity",100,0,1,0,0),step("state",30,0,0,0),step("activity",100,0,0,0,0),step("state",60,1,0,0),step("activity",Long.MAX_VALUE,Long.MAX_VALUE,Long.MAX_VALUE,1,0),step("activity",Long.MAX_VALUE,Long.MAX_VALUE-1,0,0,0),step("activity",Long.MAX_VALUE,Long.MAX_VALUE-1,0,0,1),step("state",60,1,0,1));cases.add(run("actual-activity-rate-total-wrap-and-destroy",activity));count+=activity.size();List<Map<String,Object>> operations=Arrays.asList(step("state",60,1,0,0),step("operation",6,1,1),step("operation",6,1,1),step("operation",6,0,1),step("operation",2,1,1),step("operation",6,1,1),step("operation",2,0,1),step("operation",6,0,0),step("operation",999,1,1),step("operation",6,0,1),step("delete"),step("remove"));cases.add(run("actual-operation-callbacks-no-refcount",operations));count+=operations.size();Map<String,Object> output=new TreeMap<>();output.put("cases",cases);Files.write(Paths.get(args[0]),BEncoder.encode(output));System.out.println("ORIGINAL_DOWNLOAD_STATE_TAGS cases="+cases.size()+" steps="+count+" actual_state_and_activity_methods=1 PASS");com.biglybt.core.security.SESecurityManager.exitVM(0);}
}
