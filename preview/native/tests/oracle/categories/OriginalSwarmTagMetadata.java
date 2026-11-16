// SPDX-License-Identifier: GPL-2.0-or-later
package com.biglybt.core.tag.impl;
import com.biglybt.core.config.COConfigurationManager;
import com.biglybt.core.internat.MessageText;
import com.biglybt.core.tag.*;
import com.biglybt.core.util.BEncoder;
import java.lang.reflect.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;
import sun.misc.Unsafe;
/** Actual saved-map constructor and TT_NONE/no-add/remove boundary, no Core. */
public final class OriginalSwarmTagMetadata {
 static final Unsafe U;static{try{Field f=Unsafe.class.getDeclaredField("theUnsafe");f.setAccessible(true);U=(Unsafe)f.get(null);}catch(Exception e){throw new RuntimeException(e);}}
 static void set(Object o,Class<?> owner,String name,Object value)throws Exception{Field f=owner.getDeclaredField(name);f.setAccessible(true);f.set(o,value);}
 static Taggable subject(int kind){return (Taggable)Proxy.newProxyInstance(OriginalSwarmTagMetadata.class.getClassLoader(),new Class[]{Taggable.class},(p,m,a)->m.getName().equals("getTaggableType")?kind:m.getName().equals("hashCode")?System.identityHashCode(p):m.getName().equals("equals")?p==a[0]:null);}
 public static void main(String[] args)throws Exception{
  COConfigurationManager.setParameter("tagmanager.enable",false);set(null,TagManagerImpl.class,"singleton",U.allocateInstance(TagManagerImpl.class));MessageText.changeLocale(Locale.ENGLISH);
  TagTypeSwarmTag type=(TagTypeSwarmTag)U.allocateInstance(TagTypeSwarmTag.class);set(type,TagTypeBase.class,"tag_type",7);
  Class<?> implementation=Class.forName(TagTypeSwarmTag.class.getName()+"$TagSwarmTagImpl");Constructor<?> constructor=implementation.getDeclaredConstructor(TagTypeBase.class,int.class,Map.class);constructor.setAccessible(true);
  List<Object> rows=new ArrayList<>();for(int id:new int[]{0,1,17,Integer.MAX_VALUE})for(String name:new String[]{"Generated swarm","tag.type.ds.seed","Café","tag.missing.generated"})for(int saved=0;saved<2;saved++){
   Map<String,Object> input=new HashMap<>();input.put("n",name.getBytes(StandardCharsets.UTF_8));if(saved!=0){input.put("o",Arrays.asList("VKVKVKVKVKVKVKVKVKVKVKVKVKVKVKVK".getBytes(StandardCharsets.US_ASCII),"unresolved-generated".getBytes(StandardCharsets.US_ASCII)));input.put("p",Arrays.asList(Collections.singletonMap("a",100L),Collections.singletonMap("a",200L)));}
   Tag tag=(Tag)constructor.newInstance(type,id,input);Map<String,Object> row=new TreeMap<>();row.put("id",(long)id);row.put("uid",tag.getTagUID());row.put("raw",name);row.put("name",tag.getTagName(true));row.put("saved_members",(long)saved);row.put("taggable_types",(long)tag.getTaggableTypes());
   List<Object> attempts=new ArrayList<>();for(int target:new int[]{Taggable.TT_DOWNLOAD,Taggable.TT_PEER,Taggable.TT_NONE}){Taggable value=subject(target);Map<String,Object> attempt=new TreeMap<>();attempt.put("target",(long)target);attempt.put("before",tag.hasTaggable(value)?1L:0L);tag.addTaggable(value);attempt.put("after_add",tag.hasTaggable(value)?1L:0L);tag.removeTaggable(value);attempt.put("after_remove",tag.hasTaggable(value)?1L:0L);attempts.add(attempt);}row.put("attempts",attempts);row.put("tagged_count",(long)tag.getTagged().size());rows.add(row);
  }
  Map<String,Object> result=new TreeMap<>();result.put("rows",rows);Files.write(Paths.get(args[0]),BEncoder.encode(result));System.out.println("ORIGINAL_SWARM_TAG_METADATA rows="+rows.size()+" attempted_memberships="+(rows.size()*3)+" actual_map_constructor_and_membership=1 PASS");com.biglybt.core.security.SESecurityManager.exitVM(0);
 }
}
