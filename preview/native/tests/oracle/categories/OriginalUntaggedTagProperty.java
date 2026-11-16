// SPDX-License-Identifier: GPL-2.0-or-later
package com.biglybt.core.tag.impl;
import com.biglybt.core.config.COConfigurationManager;
import com.biglybt.core.util.BEncoder;
import java.lang.reflect.*;
import java.nio.file.*;
import java.util.*;
import sun.misc.Unsafe;
/** Actual nullable Boolean manager reader/writer with inert dirty dispatch. */
public final class OriginalUntaggedTagProperty {
 static final Unsafe U;static{try{Field f=Unsafe.class.getDeclaredField("theUnsafe");f.setAccessible(true);U=(Unsafe)f.get(null);}catch(Exception e){throw new RuntimeException(e);}}
 static void set(Object o,Class<?> c,String name,Object value)throws Exception{Field f=c.getDeclaredField(name);f.setAccessible(true);f.set(o,value);}
 public static void main(String[] args)throws Exception{
  COConfigurationManager.setParameter("tagmanager.enable",false);
  TagManagerImpl manager=(TagManagerImpl)U.allocateInstance(TagManagerImpl.class);set(null,TagManagerImpl.class,"singleton",manager);set(manager,TagManagerImpl.class,"config_dirty",true);
  TagTypeDownloadManual type=(TagTypeDownloadManual)U.allocateInstance(TagTypeDownloadManual.class);set(type,TagTypeBase.class,"tag_type",3);
  TagDownloadWithState tag=(TagDownloadWithState)U.allocateInstance(TagDownloadWithState.class);set(tag,TagBase.class,"tag_id",1);set(tag,TagBase.class,"tag_type",type);
  Long[] variants={null,0L,1L,2L,-1L,4294967297L};Boolean[] values={null,false,true};List<Object> rows=new ArrayList<>();
  for(Long old:variants)for(Boolean value:values){
   Map<String,Object> attrs=new HashMap<>();attrs.put("future","opaque");if(old!=null)attrs.put("pp.untagged",old);
   Map<String,Object> record=new HashMap<>();record.put("n","Generated");record.put("c",attrs);Map<String,Object> tags=new HashMap<>();tags.put("1",record);Map<String,Object> config=new HashMap<>();config.put("3",tags);set(manager,TagManagerImpl.class,"config",config);
   Map<String,Object> row=new TreeMap<>();row.put("before_present",old==null?0L:1L);row.put("before",old==null?0L:old);row.put("read",old==null?-1L:manager.readBooleanAttribute(type,tag,"pp.untagged",null)?1L:0L);row.put("value",value==null?-1L:value?1L:0L);row.put("changed",manager.writeBooleanAttribute(type,tag,"pp.untagged",value)?1L:0L);row.put("after_present",attrs.containsKey("pp.untagged")?1L:0L);row.put("after",attrs.getOrDefault("pp.untagged",0L));row.put("opaque",attrs.get("future"));rows.add(row);
  }
  Map<String,Object> out=new TreeMap<>();out.put("rows",rows);Files.write(Paths.get(args[0]),BEncoder.encode(out));System.out.println("ORIGINAL_UNTAGGED_TAG_PROPERTY rows="+rows.size()+" actual_boolean_reader_writer=1 PASS");com.biglybt.core.security.SESecurityManager.exitVM(0);
 }
}
