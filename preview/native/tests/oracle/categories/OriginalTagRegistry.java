// SPDX-License-Identifier: GPL-2.0-or-later
package com.biglybt.core.tag.impl;
import com.biglybt.core.tag.*;
import com.biglybt.core.internat.MessageText;
import com.biglybt.core.util.*;
import java.lang.reflect.*;
import java.nio.file.*;
import java.util.*;
import java.util.concurrent.atomic.AtomicInteger;
import sun.misc.Unsafe;
/** Actual metadata/lookup/cache methods over generated records, no live tagger. */
public final class OriginalTagRegistry {
 static final Unsafe U;
 static {try{Field f=Unsafe.class.getDeclaredField("theUnsafe");f.setAccessible(true);U=(Unsafe)f.get(null);}catch(Exception e){throw new RuntimeException(e);}}
 static void set(Object o,Class<?> owner,String name,Object value)throws Exception{Field f=owner.getDeclaredField(name);f.setAccessible(true);f.set(o,value);}
 static Object get(Object o,Class<?> owner,String name)throws Exception{Field f=owner.getDeclaredField(name);f.setAccessible(true);return f.get(o);}
 static Taggable subject(String name,int kind){Map<String,Object> data=new HashMap<>();return (Taggable)Proxy.newProxyInstance(OriginalTagRegistry.class.getClassLoader(),new Class[]{Taggable.class},(p,m,a)->{switch(m.getName()){case "getTaggableType":return kind;case "getTaggableID":return name;case "getTaggableTransientProperty":return data.get(a[0]);case "setTaggableTransientProperty":data.put((String)a[0],a[1]);return null;case "hashCode":return System.identityHashCode(p);case "equals":return p==a[0];case "toString":return name;default:return null;}});}
 static TagTypeDownloadManual type(int id)throws Exception{TagTypeDownloadManual type=(TagTypeDownloadManual)U.allocateInstance(TagTypeDownloadManual.class);set(type,TagTypeBase.class,"tag_type",id);set(type,TagTypeBase.class,"tags_or_tag_membership_mut",new AtomicInteger());set(type,TagTypeBase.class,"TTP_TAGS_FOR_TAGGABLE_CACHE","TagTypeBase::tags_for_taggable_cache::"+id);set(type,TagTypeWithState.class,"tags",new CopyOnWriteList<Tag>());return type;}
 static Tag tag(TagTypeDownloadManual type,int id,String name,Taggable member,int kind)throws Exception{
  TagDownloadWithState tag=(TagDownloadWithState)U.allocateInstance(TagDownloadWithState.class);set(tag,TagBase.class,"tag_type",type);set(tag,TagBase.class,"tag_id",id);set(tag,TagBase.class,"tag_name",name);CopyOnWriteSet<Taggable> members=new CopyOnWriteSet<>(true);if(member!=null)members.add(member);set(tag,TagWithState.class,"objects",members);
  // Peer-set flag input is supplied through its interface; name/UID/member
  // methods still delegate to the actual allocated original tag implementation.
  Tag exposed=kind==Taggable.TT_DOWNLOAD?tag:(Tag)Proxy.newProxyInstance(OriginalTagRegistry.class.getClassLoader(),new Class[]{Tag.class},(p,m,a)->m.getName().equals("getTaggableTypes")?kind:m.invoke(tag,a));
  @SuppressWarnings("unchecked") CopyOnWriteList<Tag> values=(CopyOnWriteList<Tag>)get(type,TagTypeWithState.class,"tags");values.add(exposed);return exposed;
 }
 static List<Object> uids(List<Tag> tags){List<Object> values=new ArrayList<>();for(Tag t:tags)values.add(t.getTagUID());return values;}
 static Map<String,Object> run(boolean reverse)throws Exception{
  Taggable download=subject("generated-download",Taggable.TT_DOWNLOAD),peer=subject("generated-peer",Taggable.TT_PEER),combined=subject("generated-combined",Taggable.TT_DOWNLOAD|Taggable.TT_PEER);
  TagTypeDownloadManual manual=type(3),category=type(1),state=type(2),peers=type(4),internal=type(5),swarm=type(7),suggestions=type(6);
  tag(manual,51,"Shared",download,2);tag(manual,13,"Shared",null,2);tag(manual,77,"tag.name.product.updates",download,2);tag(manual,21,"用户标签",download,2);tag(manual,22,"Café",download,2);tag(manual,23,"Cafe\u0301",null,2);
  tag(category,901,"Shared",download,2);tag(category,902,"All",download,2);tag(state,1,"tag.type.ds.down",download,2);tag(state,2,"tag.type.ds.seed",null,2);tag(peers,42,"Shared",peer,Taggable.TT_PEER);tag(internal,9,"Internal literal",null,2);tag(swarm,8,"Shared",download,2);tag(swarm,10,"tag.generated.missing",null,2);tag(suggestions,3,"Shared",null,2);
  List<TagTypeBase> types=new ArrayList<>(Arrays.asList(manual,category,peers,internal,state,swarm,suggestions));if(reverse)Collections.reverse(types);TagManagerImpl manager=(TagManagerImpl)U.allocateInstance(TagManagerImpl.class);CopyOnWriteList<TagTypeBase> registered=new CopyOnWriteList<>();for(TagTypeBase t:types)registered.add(t);set(manager,TagManagerImpl.class,"tag_types",registered);
  List<Object> input=new ArrayList<>();for(TagTypeBase t:types){Map<String,Object> row=new TreeMap<>();row.put("type",(long)t.getTagType());List<Object> entries=new ArrayList<>();for(Tag tag:t.getTags()){Map<String,Object> e=new TreeMap<>();e.put("id",(long)tag.getTagID());e.put("uid",tag.getTagUID());e.put("name",tag.getTagName(true));e.put("taggable_type",(long)tag.getTaggableTypes());e.put("download_member",tag.hasTaggable(download)?1L:0L);e.put("peer_member",tag.hasTaggable(peer)?1L:0L);entries.add(e);}row.put("entries",entries);input.add(row);}
  List<Object> queries=new ArrayList<>();for(String name:new String[]{"Shared","shared","BiglyBT Updates","tag.name.product.updates","Downloading","Seeding","用户标签","Café","Cafe\u0301","!tag.generated.missing!","Absent",""}){Map<String,Object> q=new TreeMap<>();q.put("name",name);q.put("matches",uids(manager.lookupTagsByName(name)));queries.add(q);}
  Map<String,Object> result=new TreeMap<>();result.put("types",input);result.put("queries",queries);result.put("download",uids(manager.getTagsForTaggable(download)));result.put("peer",uids(manager.getTagsForTaggable(peer)));result.put("combined",uids(manager.getTagsForTaggable(combined)));result.put("reverse",reverse?1L:0L);return result;
 }
 public static void main(String[] args)throws Exception{MessageText.changeLocale(Locale.ENGLISH);set(null,TagManagerImpl.class,"singleton",U.allocateInstance(TagManagerImpl.class));Map<String,Object> out=new TreeMap<>();out.put("cases",Arrays.asList(run(false),run(true)));Files.write(Paths.get(args[0]),BEncoder.encode(out));System.out.println("ORIGINAL_TAG_REGISTRY type_orders=2 name_queries=24 membership_queries=6 actual_methods=1 PASS");com.biglybt.core.security.SESecurityManager.exitVM(0);}
}
