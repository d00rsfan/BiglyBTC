// SPDX-License-Identifier: GPL-2.0-or-later
package com.biglybt.core.tag.impl;
import com.biglybt.core.download.DownloadManager;
import com.biglybt.core.util.*;
import java.lang.reflect.*;
import java.lang.ref.WeakReference;
import java.nio.file.*;
import java.util.*;
import sun.misc.Unsafe;
/** Actual saved Long getter,intValue and private rate normalizer with empty download membership. */
public final class OriginalTagRateLimits {
 static final Unsafe U;static{try{Field f=Unsafe.class.getDeclaredField("theUnsafe");f.setAccessible(true);U=(Unsafe)f.get(null);}catch(Exception e){throw new RuntimeException(e);}}
 static void set(Object o,Class<?> c,String n,Object v)throws Exception{Field f=c.getDeclaredField(n);f.setAccessible(true);f.set(o,v);}
 static final class RateTag extends TagDownloadWithState {
  RateTag(){super(null,1,"Generated",true,true,true,false,0);}
  @Override public Set<DownloadManager> getTaggedDownloads(){return Collections.emptySet();}
 }
 public static void main(String[] args)throws Exception{
  TagManagerImpl manager=(TagManagerImpl)U.allocateInstance(TagManagerImpl.class);set(null,TagManagerImpl.class,"singleton",manager);
  TagTypeDownloadManual type=(TagTypeDownloadManual)U.allocateInstance(TagTypeDownloadManual.class);set(type,TagTypeBase.class,"tag_type",3);
  RateTag tag=(RateTag)U.allocateInstance(RateTag.class);set(tag,TagBase.class,"tag_id",1);set(tag,TagBase.class,"tag_type",type);set(tag,TagDownloadWithState.class,"rate_lock",new Object());
  Method normalize=TagDownloadWithState.class.getDeclaredMethod("setRateLimit",int.class,boolean.class);normalize.setAccessible(true);
  Long[] variants={null,0L,1L,2L,1024L,-1L,-2L,(long)Integer.MIN_VALUE,(long)Integer.MAX_VALUE,4294967296L,4294967297L,Long.MIN_VALUE,Long.MAX_VALUE};List<Object> rows=new ArrayList<>();
  for(boolean upload:new boolean[]{true,false})for(Long value:variants)for(int prior:new int[]{0,-1,1024}){
   String key=upload?"rl.up":"rl.down";Map<String,Object> properties=new HashMap<>();if(value!=null)properties.put(key,value);
   Map<String,Object> record=new HashMap<>();record.put("c",properties);Map<String,Object> tags=new HashMap<>();tags.put("1",record);Map<String,Object> config=new HashMap<>();config.put("3",tags);set(manager,TagManagerImpl.class,"config",config);set(manager,TagManagerImpl.class,"config_ref",new WeakReference<>(config));
   set(tag,TagDownloadWithState.class,"upload_rate_limit",prior);set(tag,TagDownloadWithState.class,"download_rate_limit",prior);
   int narrowed=tag.readLongAttribute(key,0L).intValue();normalize.invoke(tag,narrowed,upload);
   String raw="d1:cd"+(value==null?"":key.length()+":"+key+"i"+value+"e")+"ee";Map<String,Object> row=new TreeMap<>();row.put("record",raw.getBytes("ISO-8859-1"));row.put("upload",upload?1L:0L);row.put("prior",(long)prior);row.put("narrowed",Integer.toString(narrowed));row.put("result",(long)(upload?tag.getTagUploadLimit():tag.getTagDownloadLimit()));rows.add(row);
  }
  Files.write(Paths.get(args[0]),BEncoder.encode(Collections.<String,Object>singletonMap("rows",rows)));System.out.println("ORIGINAL_TAG_RATE_LIMITS rows="+rows.size()+" actual_saved_getter_intvalue_private_normalizer=1 empty_download_boundary=1 PASS");com.biglybt.core.security.SESecurityManager.exitVM(0);
 }
}
