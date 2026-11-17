// SPDX-License-Identifier: GPL-2.0-or-later
package com.biglybt.core.tag.impl;
import com.biglybt.core.tag.*;
import com.biglybt.core.util.*;
import java.lang.reflect.*;
import java.lang.ref.WeakReference;
import java.nio.file.*;
import java.util.*;
import sun.misc.Unsafe;
/** Actual action setter/manager writer;only metadata-listener delivery is recorded. */
public final class OriginalAssignmentSetter {
 static final Unsafe U;static{try{Field f=Unsafe.class.getDeclaredField("theUnsafe");f.setAccessible(true);U=(Unsafe)f.get(null);}catch(Exception e){throw new RuntimeException(e);}}
 static void set(Object o,Class<?> c,String n,Object v)throws Exception{Field f=c.getDeclaredField(n);f.setAccessible(true);f.set(o,v);}
 static final class Type extends TagTypeDownloadManual {Type(){super(null);}int notifications;@Override protected void fireMetadataChanged(Tag tag){notifications++;}}
 static byte[] record(Map<String,Object> properties)throws Exception{StringBuilder out=new StringBuilder("d1:cd");for(Map.Entry<String,Object> entry:properties.entrySet()){out.append(entry.getKey().length()).append(':').append(entry.getKey());Object value=entry.getValue();if(value instanceof Long)out.append('i').append(value).append('e');else{String text=(String)value;out.append(text.length()).append(':').append(text);}}out.append("e1:n9:Generated1:ole1:plee");return out.toString().getBytes("ISO-8859-1");}
 public static void main(String[] args)throws Exception{
  TagManagerImpl manager=(TagManagerImpl)U.allocateInstance(TagManagerImpl.class);set(null,TagManagerImpl.class,"singleton",manager);set(manager,TagManagerImpl.class,"config_dirty",true);
  Type type=(Type)U.allocateInstance(Type.class);set(type,TagTypeBase.class,"tag_type",3);
  TagDownloadWithState tag=(TagDownloadWithState)U.allocateInstance(TagDownloadWithState.class);set(tag,TagBase.class,"tag_type",type);set(tag,TagBase.class,"tag_id",1);
  Long[] oldValues={null,0L,1L,2L,-1L,4294967297L,Long.MIN_VALUE,(long)Integer.MIN_VALUE,Long.MAX_VALUE};List<Object> rows=new ArrayList<>();
  for(int action:new int[]{2,8,16,4,32768,64,128})for(Long old:oldValues)for(boolean value:new boolean[]{false,true}){
   Map<String,Object> properties=new TreeMap<>();properties.put("future","opaque");properties.put("pp.32",1L);if(old!=null)properties.put("pp."+action,old);
   Map<String,Object> config=new HashMap<>();Map<String,Object> rec=new HashMap<>();rec.put("c",properties);Map<String,Object> tags=new HashMap<>();tags.put("1",rec);config.put("3",tags);
   set(manager,TagManagerImpl.class,"config",config);set(manager,TagManagerImpl.class,"config_ref",new WeakReference<>(config));
   Map<String,Object> row=new TreeMap<>();row.put("before",record(properties));row.put("action",(long)action);row.put("enabled",value?1L:0L);type.notifications=0;
   tag.setActionEnabled(action,value);row.put("after",record(properties));row.put("metadata",(long)type.notifications);rows.add(row);
  }
  Files.write(Paths.get(args[0]),BEncoder.encode(Collections.<String,Object>singletonMap("rows",rows)));System.out.println("ORIGINAL_ASSIGNMENT_SETTER rows="+rows.size()+" actual_setter_manager_writer=1 metadata_delivery_recorded=1 PASS");com.biglybt.core.security.SESecurityManager.exitVM(0);
 }
}
