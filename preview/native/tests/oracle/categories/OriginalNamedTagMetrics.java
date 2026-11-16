// SPDX-License-Identifier: GPL-2.0-or-later
package com.biglybt.core.tag.impl;
import com.biglybt.core.download.DownloadManager;
import com.biglybt.core.tag.*;
import com.biglybt.core.util.*;
import java.lang.reflect.*;
import java.nio.file.*;
import java.util.*;
/** Actual constructor/function evaluation with supplied clock/added-time and
 * generated download positions. Membership/count are real TagWithState data. */
public final class OriginalNamedTagMetrics {
 static final long WALL=1700000000000L;
 static Object proxy(Class<?> c,InvocationHandler h){return Proxy.newProxyInstance(c.getClassLoader(),new Class[]{c},h);}
 static Taggable download(int position){return (DownloadManager)proxy(DownloadManager.class,(p,m,a)->{switch(m.getName()){case "getTaggableType":return Taggable.TT_DOWNLOAD;case "getPosition":return position;case "getTagMutationCount":return 0L;case "getTaggableTransientProperty":return null;case "setTaggableTransientProperty":return null;case "hashCode":return System.identityHashCode(p);case "equals":return p==a[0];default:return null;}});}
 @SuppressWarnings("unchecked") static Map<String,Object> run(String function,long added,boolean member)throws Exception{
  TagManagerImpl manager=(TagManagerImpl)OriginalOrdinaryTagMembership.U.allocateInstance(TagManagerImpl.class);OriginalOrdinaryTagMembership.set(null,TagManagerImpl.class,"singleton",manager);TagTypeDownloadManual type=OriginalPeerSetConstraintFacts.type(3);CopyOnWriteList<TagTypeBase> types=new CopyOnWriteList<>();types.add(type);OriginalOrdinaryTagMembership.set(manager,TagManagerImpl.class,"tag_types",types);Field field=TagTypeWithState.class.getDeclaredField("tags");field.setAccessible(true);CopyOnWriteList<Tag> registered=(CopyOnWriteList<Tag>)field.get(type);
  var root=OriginalPeerSetConstraintFacts.tag(type,0);var target=OriginalPeerSetConstraintFacts.tag(type,1);Map<Taggable,Long> times=new IdentityHashMap<>();Taggable subject=download(5);int[] positions={3,5,-1};long[] otherTimes={WALL-4000,0,-1};
  for(int i=0;i<positions.length;i++){Taggable peer=download(positions[i]);target.members.add(peer);times.put(peer,otherTimes[i]);}if(member){target.members.add(subject);}times.put(subject,added);
  Tag named=(Tag)proxy(Tag.class,(p,m,a)->{if(m.getName().equals("getTagName"))return "Generated metrics";if(m.getName().equals("getTaggableAddedTime"))return times.getOrDefault(a[0],-1L);return m.invoke(target,a);});registered.add(root);registered.add(named);
  Object handler=OriginalOrdinaryTagMembership.U.allocateInstance(TagPropertyConstraintHandler.class);OriginalOrdinaryTagMembership.set(handler,TagPropertyConstraintHandler.class,"tag_manager",manager);OriginalOrdinaryTagMembership.set(handler,TagPropertyConstraintHandler.class,"constrained_tags",new HashMap<>());OriginalOrdinaryTagMembership.set(handler,TagPropertyConstraintHandler.class,"apply_history",new HashMap<>());
  if(SystemTime.getCurrentTime()!=WALL)throw new AssertionError("Clock boundary changed");String expression=function.replace("NAME","\"Generated metrics\"");Class<?> tc=OriginalOrdinaryTagMembership.CONSTRAINT;Constructor<?> ctor=tc.getDeclaredConstructor(TagPropertyConstraintHandler.class,Tag.class,String.class,String.class,boolean.class);ctor.setAccessible(true);Object rule=ctor.newInstance(handler,root,expression,"am=0;",true);Field exprField=tc.getDeclaredField("expr");exprField.setAccessible(true);Object expr=exprField.get(rule);if(expr==null)throw new AssertionError("Actual named expression refused: "+expression);Method eval=expr.getClass().getDeclaredMethod("eval",Map.class,DownloadManager.class,List.class,StringBuilder.class);eval.setAccessible(true);Object result=eval.invoke(expr,new HashMap<>(),subject,manager.getTagsForTaggable(subject),null);
  Map<String,Object> row=new TreeMap<>();row.put("function",function);row.put("wall",WALL);row.put("added",added);row.put("member",member?1L:0L);row.put("position",5L);row.put("other_positions",Arrays.asList(3L,5L,-1L));row.put("other_added",Arrays.asList(otherTimes[0],otherTimes[1],otherTimes[2]));row.put("count",(long)target.getTaggedCount());row.put("value",((Number)result).longValue());return row;
 }
 public static void main(String[] args)throws Exception{
  Class<?> provider=Class.forName(SystemTime.class.getName()+"$SystemTimeProvider");Field instance=SystemTime.class.getDeclaredField("instance");instance.setAccessible(true);Object previous=instance.get(null);Object clock=proxy(provider,(p,m,a)->m.getName().equals("getTime")?WALL:1000000L);Object clockBase=OriginalOrdinaryTagMembership.U.staticFieldBase(instance);long clockOffset=OriginalOrdinaryTagMembership.U.staticFieldOffset(instance);OriginalOrdinaryTagMembership.U.putObjectVolatile(clockBase,clockOffset,clock);if(SystemTime.getCurrentTime()!=WALL)throw new AssertionError("Supplied clock was not installed");
  List<Object> rows=new ArrayList<>();try{for(String function:new String[]{"hasTagAge(NAME)","countTag(NAME)","tagPosition(NAME)","tagPosition(NAME,0)","tagPosition(NAME,2)"})for(long added:new long[]{-1L,0L,1L,WALL-999,WALL-1000,WALL+1,Long.MAX_VALUE,Long.MIN_VALUE})for(boolean member:new boolean[]{false,true})rows.add(run(function,added,member));}finally{OriginalOrdinaryTagMembership.U.putObjectVolatile(clockBase,clockOffset,previous);}
  Map<String,Object> out=new TreeMap<>();out.put("rows",rows);Files.write(Paths.get(args[0]),BEncoder.encode(out));System.out.println("ORIGINAL_NAMED_TAG_METRICS rows="+rows.size()+" actual_constructor_function_membership_count=1 clock_and_added_time_boundaries=1 PASS");com.biglybt.core.security.SESecurityManager.exitVM(0);
 }
}
