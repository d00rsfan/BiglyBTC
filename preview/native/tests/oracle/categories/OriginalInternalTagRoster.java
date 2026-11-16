// SPDX-License-Identifier: GPL-2.0-or-later
package com.biglybt.core.tag.impl;
import com.biglybt.core.config.COConfigurationManager;
import com.biglybt.core.tag.*;
import com.biglybt.core.util.*;
import java.lang.ref.WeakReference;
import java.lang.reflect.*;
import java.nio.file.*;
import java.util.*;
import java.util.concurrent.atomic.*;
/** Actual internal type/tag constructor and roster operations. Inert manager
 * backing/empty listeners and resolver boundary; no Core/provider is started. */
public final class OriginalInternalTagRoster {
 static void set(Object o,Class<?> c,String name,Object value)throws Exception{OriginalOrdinaryTagMembership.set(o,c,name,value);}
 static Map<String,Object> snapshot(String action,TagTypeDownloadInternal type,List<Tag> allocated){Map<String,Object> row=new TreeMap<>();row.put("action",action);row.put("type",(long)type.getTagType());row.put("persistent",type.isTagTypePersistent()?1L:0L);row.put("auto",type.isTagTypeAuto()?1L:0L);row.put("properties",type.hasTagTypeFeature(TagFeature.TF_PROPERTIES)?1L:0L);List<Object> tags=new ArrayList<>();for(Tag tag:type.getTags()){Map<String,Object> item=new TreeMap<>();item.put("id",(long)tag.getTagID());item.put("uid",tag.getTagUID());item.put("name",tag.getTagName(false));item.put("raw_name",((TagBase)tag).getTagNameRaw());item.put("localized_name",tag.getTagName(true));item.put("kind",(long)tag.getTaggableTypes());item.put("count",(long)tag.getTaggedCount());tags.add(item);}row.put("tags",tags);List<Object> ids=new ArrayList<>();for(Tag tag:allocated)ids.add((long)tag.getTagID());row.put("allocated",ids);return row;}
 public static void main(String[] args)throws Exception{
  COConfigurationManager.initialiseFromMap(new HashMap<>());COConfigurationManager.setParameter("tagmanager.enable",true);
  TagManagerImpl manager=(TagManagerImpl)OriginalOrdinaryTagMembership.U.allocateInstance(TagManagerImpl.class);
  for(Field f:TagManagerImpl.class.getDeclaredFields())if(!Modifier.isStatic(f.getModifiers())){f.setAccessible(true);Class<?> t=f.getType();if(t==CopyOnWriteList.class)f.set(manager,new CopyOnWriteList<>());else if(t==Map.class)f.set(manager,new HashMap<>());else if(t==Set.class)f.set(manager,new HashSet<>());else if(t==List.class)f.set(manager,new ArrayList<>());else if(t==AtomicInteger.class)f.set(manager,new AtomicInteger());else if(t==AtomicLong.class)f.set(manager,new AtomicLong());}
  Map<String,Object> config=new HashMap<>();set(manager,TagManagerImpl.class,"config_ref",new WeakReference<>(config));set(null,TagManagerImpl.class,"singleton",manager);
  TagTypeDownloadInternal type=new TagTypeDownloadInternal(null);Field bound=TagTypeBase.class.getDeclaredField("manager");bound.setAccessible(true);if(bound.get(null)!=manager)throw new AssertionError("Actual type uses a different manager");
  List<Tag> allocated=new ArrayList<>();List<Object> rows=new ArrayList<>();rows.add(snapshot("empty",type,allocated));
  Tag first=type.createTag("Generated first",false);allocated.add(first);rows.add(snapshot("detached",type,allocated));
  Tag second=type.createTag("Generated second",true);allocated.add(second);rows.add(snapshot("registered-second",type,allocated));
  type.addTag(first);rows.add(snapshot("attach-first",type,allocated));
  first.setTagName("Generated renamed");rows.add(snapshot("rename-first",type,allocated));
  Tag duplicate=type.createTag("Generated second",true);allocated.add(duplicate);rows.add(snapshot("duplicate-name",type,allocated));
  second.removeTag();rows.add(snapshot("remove-second",type,allocated));
  Tag next=type.createTag("Generated final",true);allocated.add(next);rows.add(snapshot("next-id",type,allocated));
  Map<String,Object> out=new TreeMap<>();out.put("rows",rows);Files.write(Paths.get(args[0]),BEncoder.encode(out));System.out.println("ORIGINAL_INTERNAL_TAG_ROSTER rows="+rows.size()+" actual_type_tag_constructor_and_roster=1 inert_manager_boundary=1 PASS");com.biglybt.core.security.SESecurityManager.exitVM(0);
 }
}
