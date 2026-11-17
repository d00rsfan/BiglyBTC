// SPDX-License-Identifier: GPL-2.0-or-later
package com.biglybt.core.tag.impl;
import com.biglybt.core.tag.*;
import com.biglybt.core.download.DownloadManager;
import com.biglybt.core.util.*;
import java.lang.reflect.*;
import java.lang.ref.WeakReference;
import java.nio.file.*;
import java.util.*;
import sun.misc.Unsafe;
/** Actual public rate setters,empty download cohort and actual manager writer. */
public final class OriginalTagRateSettings {
 static final Unsafe U;static{try{Field f=Unsafe.class.getDeclaredField("theUnsafe");f.setAccessible(true);U=(Unsafe)f.get(null);}catch(Exception e){throw new RuntimeException(e);}}
 static void set(Object o,Class<?> c,String n,Object v)throws Exception{Field f=c.getDeclaredField(n);f.setAccessible(true);f.set(o,v);}
 static final class Type extends TagTypeDownloadManual{Type(){super(null);}int notifications;@Override protected void fireMetadataChanged(Tag tag){notifications++;}}
 static final class RateTag extends TagDownloadWithState{RateTag(){super(null,1,"Generated",true,true,true,false,0);}@Override public Set<DownloadManager> getTaggedDownloads(){return Collections.emptySet();}}
 static byte[] record(Map<String,Object> properties)throws Exception{StringBuilder out=new StringBuilder("d1:cd");for(Map.Entry<String,Object> entry:properties.entrySet()){out.append(entry.getKey().length()).append(':').append(entry.getKey());Object value=entry.getValue();if(value instanceof Long)out.append('i').append(value).append('e');else{String text=(String)value;out.append(text.length()).append(':').append(text);}}out.append("e1:n9:Generated1:ole1:plee");return out.toString().getBytes("ISO-8859-1");}
 public static void main(String[] args)throws Exception{
  TagManagerImpl manager=(TagManagerImpl)U.allocateInstance(TagManagerImpl.class);set(null,TagManagerImpl.class,"singleton",manager);set(manager,TagManagerImpl.class,"config_dirty",true);
  Type type=(Type)U.allocateInstance(Type.class);set(type,TagTypeBase.class,"tag_type",3);RateTag tag=(RateTag)U.allocateInstance(RateTag.class);
  set(tag,TagBase.class,"tag_id",1);set(tag,TagBase.class,"tag_type",type);set(tag,TagDownloadWithState.class,"rate_lock",new Object());set(tag,TagDownloadWithState.class,"do_up",true);set(tag,TagDownloadWithState.class,"do_down",true);
  Method normalize=TagDownloadWithState.class.getDeclaredMethod("setRateLimit",int.class,boolean.class);normalize.setAccessible(true);
  Long[] variants={null,0L,1L,10L,1024L,-1L,-2L,(long)Integer.MIN_VALUE,(long)Integer.MAX_VALUE,4294967296L,4294967297L,4294967306L,4294968320L,Long.MIN_VALUE,Long.MAX_VALUE};List<Object> rows=new ArrayList<>();
  for(boolean up:new boolean[]{true,false})for(Long old:variants)for(int desired:new int[]{0,10,1024,2147483646}){
   String key=up?"rl.up":"rl.down";Map<String,Object> properties=new TreeMap<>();properties.put("future","opaque");properties.put(up?"rl.down":"rl.up",2048L);if(old!=null)properties.put(key,old);
   Map<String,Object> config=new HashMap<>();Map<String,Object> rec=new HashMap<>();rec.put("c",properties);Map<String,Object> tags=new HashMap<>();tags.put("1",rec);config.put("3",tags);set(manager,TagManagerImpl.class,"config",config);set(manager,TagManagerImpl.class,"config_ref",new WeakReference<>(config));
   set(tag,TagDownloadWithState.class,"upload_rate_limit",2048);set(tag,TagDownloadWithState.class,"download_rate_limit",2048);normalize.invoke(tag,tag.readLongAttribute(key,0L).intValue(),up);
   Map<String,Object> row=new TreeMap<>();row.put("before",record(properties));row.put("upload",up?1L:0L);row.put("desired",(long)desired);row.put("prior",(long)(up?tag.getTagUploadLimit():tag.getTagDownloadLimit()));type.notifications=0;
   if(up)tag.setTagUploadLimit(desired);else tag.setTagDownloadLimit(desired);
   row.put("after",record(properties));row.put("metadata",(long)type.notifications);row.put("result",(long)(up?tag.getTagUploadLimit():tag.getTagDownloadLimit()));rows.add(row);
  }
  Files.write(Paths.get(args[0]),BEncoder.encode(Collections.<String,Object>singletonMap("rows",rows)));System.out.println("ORIGINAL_TAG_RATE_SETTINGS rows="+rows.size()+" actual_public_setter_manager_writer=1 empty_downloads=1 PASS");com.biglybt.core.security.SESecurityManager.exitVM(0);
 }
}
