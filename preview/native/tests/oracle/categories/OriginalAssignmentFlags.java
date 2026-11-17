// SPDX-License-Identifier: GPL-2.0-or-later
package com.biglybt.core.tag.impl;
import com.biglybt.core.tag.*;
import com.biglybt.core.util.*;
import java.lang.reflect.*;
import java.lang.ref.WeakReference;
import java.nio.file.*;
import java.util.*;
import sun.misc.Unsafe;
/** Actual supported/action getters on allocated original objects with an inert
 * manager config. No tag constructor, setter, actor or service is activated. */
public final class OriginalAssignmentFlags {
 static final Unsafe U;static{try{Field f=Unsafe.class.getDeclaredField("theUnsafe");f.setAccessible(true);U=(Unsafe)f.get(null);}catch(Exception e){throw new RuntimeException(e);}}
 static void set(Object object,Class<?> owner,String name,Object value)throws Exception{Field f=owner.getDeclaredField(name);f.setAccessible(true);f.set(object,value);}
 static String wire(Object value)throws Exception{
  if(value instanceof Long)return "i"+value+"e";
  if(value instanceof byte[]){byte[] b=(byte[])value;return b.length+":"+new String(b,"ISO-8859-1");}
  if(value instanceof List)return "le";
  if(value instanceof Map)return "de";
  throw new AssertionError("Unexpected generated value");
 }
 static Map<String,Object> run(int typeId,Map<String,Object> properties,TagManagerImpl manager)throws Exception{
  TagTypeDownloadManual type=(TagTypeDownloadManual)U.allocateInstance(TagTypeDownloadManual.class);set(type,TagTypeBase.class,"tag_type",typeId);
  TagDownloadWithState tag=(TagDownloadWithState)U.allocateInstance(TagDownloadWithState.class);set(tag,TagBase.class,"tag_type",type);set(tag,TagBase.class,"tag_id",1);
  Map<String,Object> config=Collections.<String,Object>singletonMap(Integer.toString(typeId),Collections.<String,Object>singletonMap("1",Collections.<String,Object>singletonMap("c",properties)));
  set(manager,TagManagerImpl.class,"config",config);
  set(manager,TagManagerImpl.class,"config_ref",new WeakReference<>(config));
  int enabled=0;for(int action:TagFeatureExecOnAssign.ACTIONS)if(tag.isActionEnabled(action))enabled|=action;
  StringBuilder record=new StringBuilder("d1:cd");for(Map.Entry<String,Object> entry:properties.entrySet())record.append(entry.getKey().length()).append(':').append(entry.getKey()).append(wire(entry.getValue()));record.append("ee");
  Map<String,Object> row=new TreeMap<>();row.put("type",(long)typeId);row.put("record",record.toString().getBytes("ISO-8859-1"));row.put("supported",(long)tag.getSupportedActions());row.put("enabled",(long)enabled);return row;
 }
 public static void main(String[] args)throws Exception{
  TagManagerImpl manager=(TagManagerImpl)U.allocateInstance(TagManagerImpl.class);set(null,TagManagerImpl.class,"singleton",manager);
  List<Object> rows=new ArrayList<>();Object[] values={null,0L,1L,2L,-1L,Long.MIN_VALUE,(long)Integer.MIN_VALUE,Long.MAX_VALUE,new byte[]{'1'},new ArrayList<Object>(),new TreeMap<String,Object>()};
  for(int type:new int[]{2,3,4})for(int action:TagFeatureExecOnAssign.ACTIONS)for(Object value:values){Map<String,Object> properties=new TreeMap<>();if(value!=null)properties.put("pp."+action,value);rows.add(run(type,properties,manager));}
  int[] flags={2,4,8,16,64,128,32768};for(int bits=0;bits<128;bits++){Map<String,Object> properties=new TreeMap<>();for(int at=0;at<flags.length;at++)if((bits&(1<<at))!=0)properties.put("pp."+flags[at],1L);rows.add(run(3,properties,manager));}
  Files.write(Paths.get(args[0]),BEncoder.encode(Collections.<String,Object>singletonMap("rows",rows)));
  System.out.println("ORIGINAL_ASSIGNMENT_FLAGS rows="+rows.size()+" actual_supported_enabled_getters=1 inert_allocated_config=1 PASS");com.biglybt.core.security.SESecurityManager.exitVM(0);
 }
}
