// SPDX-License-Identifier: GPL-2.0-or-later
package com.biglybt.core.tag.impl;
import com.biglybt.core.config.COConfigurationManager;
import com.biglybt.core.util.BEncoder;
import java.lang.reflect.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;
import sun.misc.Unsafe;
/** Actual protected manager writer; inert config and dirty-dispatch boundary. */
public final class OriginalTrackerTagProperty {
 static final Unsafe U;static{try{Field f=Unsafe.class.getDeclaredField("theUnsafe");f.setAccessible(true);U=(Unsafe)f.get(null);}catch(Exception e){throw new RuntimeException(e);}}
 static void set(Object o,Class<?> c,String name,Object value)throws Exception{Field f=c.getDeclaredField(name);f.setAccessible(true);f.set(o,value);}
 @SuppressWarnings("unchecked") public static void main(String[] args)throws Exception{
  COConfigurationManager.setParameter("tagmanager.enable",false);
  TagManagerImpl manager=(TagManagerImpl)U.allocateInstance(TagManagerImpl.class);set(null,TagManagerImpl.class,"singleton",manager);set(manager,TagManagerImpl.class,"config_dirty",true);
  TagTypeDownloadManual type=(TagTypeDownloadManual)U.allocateInstance(TagTypeDownloadManual.class);set(type,TagTypeBase.class,"tag_type",3);
  TagDownloadWithState tag=(TagDownloadWithState)U.allocateInstance(TagDownloadWithState.class);set(tag,TagBase.class,"tag_id",1);set(tag,TagBase.class,"tag_type",type);
  String[][] variants={null,new String[0],new String[]{"example.test"},new String[]{"EXAMPLE.TEST"},new String[]{"example.test","other.invalid:80"},new String[]{" other.invalid "},new String[]{""},new String[]{"example.test","example.test"}};
  List<Object> rows=new ArrayList<>();for(String[] old:variants)for(String[] value:variants){
   Map<String,Object> attrs=new HashMap<>();attrs.put("future","opaque");if(old!=null){List<Object> encoded=new ArrayList<>();for(String s:old){encoded.add(s.getBytes(StandardCharsets.UTF_8));}attrs.put("pp.trackers",encoded);}
   Map<String,Object> record=new HashMap<>();record.put("n","Generated");record.put("c",attrs);Map<String,Object> tags=new HashMap<>();tags.put("1",record);Map<String,Object> config=new HashMap<>();config.put("3",tags);set(manager,TagManagerImpl.class,"config",config);
   Map<String,Object> row=new TreeMap<>();row.put("before_present",old==null?0L:1L);row.put("before",old==null?Collections.emptyList():Arrays.asList(old));row.put("value_present",value==null?0L:1L);row.put("value",value==null?Collections.emptyList():Arrays.asList(value));row.put("changed",manager.writeStringListAttribute(type,tag,"pp.trackers",value)?1L:0L);row.put("after_present",attrs.containsKey("pp.trackers")?1L:0L);row.put("after",attrs.getOrDefault("pp.trackers",Collections.emptyList()));row.put("opaque",attrs.get("future"));rows.add(row);
  }
  Map<String,Object> result=new TreeMap<>();result.put("rows",rows);Files.write(Paths.get(args[0]),BEncoder.encode(result));System.out.println("ORIGINAL_TRACKER_TAG_PROPERTY rows="+rows.size()+" actual_manager_writer=1 PASS");com.biglybt.core.security.SESecurityManager.exitVM(0);
 }
}
