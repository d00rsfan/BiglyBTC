// SPDX-License-Identifier: GPL-2.0-or-later
package com.biglybt.core.tag.impl;
import com.biglybt.core.Core;
import com.biglybt.core.config.COConfigurationManager;
import com.biglybt.core.download.DownloadManager;
import com.biglybt.core.global.GlobalManager;
import com.biglybt.core.tag.*;
import com.biglybt.core.tag.TagFeatureProperties.TagProperty;
import com.biglybt.core.util.*;
import java.lang.reflect.*;
import java.nio.file.*;
import java.util.*;
import sun.misc.Unsafe;
/** Actual untagged handler and captured listeners; inert type/core/tag boundaries. */
public final class OriginalUntaggedTagEvents {
 static final Unsafe U;static{try{Field f=Unsafe.class.getDeclaredField("theUnsafe");f.setAccessible(true);U=(Unsafe)f.get(null);}catch(Exception e){throw new RuntimeException(e);}}
 static void set(Object o,Class<?> c,String name,Object value)throws Exception{Field f=c.getDeclaredField(name);f.setAccessible(true);f.set(o,value);}
 static Object proxy(Class<?> type,InvocationHandler h){return Proxy.newProxyInstance(type.getClassLoader(),new Class[]{type},h);}
 static Object identity(Object p,Method m,Object[] a){switch(m.getName()){case "hashCode":return System.identityHashCode(p);case "equals":return p==a[0];case "toString":return "generated-boundary";}throw new AssertionError("Unexpected boundary: "+m.getName());}
 public static final class Type extends TagTypeDownloadManual {public Type(){super(null);}public void addTagTypeListener(TagTypeListener l,boolean fire){}public void removeTagTypeListener(TagTypeListener l){}}
 static final class Recording implements InvocationHandler {
  final int id;final Set<Taggable> members=new LinkedHashSet<>();final List<TagListener> listeners=new ArrayList<>();final Tag tag;
  Recording(int id){this.id=id;tag=(Tag)proxy(Tag.class,this);}
  void add(Taggable dm){if(members.add(dm)){for(TagListener l:new ArrayList<>(listeners)){l.taggableAdded(tag,dm);}}}
  void remove(Taggable dm){if(members.remove(dm)){for(TagListener l:new ArrayList<>(listeners)){l.taggableRemoved(tag,dm);}}}
  public Object invoke(Object p,Method m,Object[] a){switch(m.getName()){
   case "getTagID":return id;case "getTagUID":return (3L<<32)|id;case "getTagName":return "Generated-"+id;case "getTaggableTypes":return Taggable.TT_DOWNLOAD;
   case "getTagged":return new LinkedHashSet<>(members);case "hasTaggable":return members.contains(a[0]);case "getTaggedCount":return members.size();case "addTaggable":add((Taggable)a[0]);return null;case "removeTaggable":remove((Taggable)a[0]);return null;
   case "addTagListener":listeners.add((TagListener)a[0]);if((Boolean)a[1])for(Taggable dm:new ArrayList<>(members))((TagListener)a[0]).taggableAdded(tag,dm);return null;
   case "removeTagListener":listeners.remove(a[0]);return null;
  }return identity(p,m,a);}
 }
 static void property(TagPropertyUntaggedHandler handler,Tag tag,boolean enabled)throws Exception{
  TagProperty property=(TagProperty)proxy(TagProperty.class,(p,m,a)->m.getName().equals("getTag")?tag:m.getName().equals("getBoolean")?enabled:identity(p,m,a));Method change=TagPropertyUntaggedHandler.class.getDeclaredMethod("handleProperty",TagProperty.class);change.setAccessible(true);change.invoke(handler,property);
 }
 @SuppressWarnings("unchecked") static Map<String,Object> run(int ordinaryMask,int savedMask,boolean persistent,boolean destroyed,String event)throws Exception{
  DownloadManager dm=(DownloadManager)proxy(DownloadManager.class,(p,m,a)->{switch(m.getName()){case "isPersistent":return persistent;case "isDestroyed":return destroyed;case "getTaggableType":return Taggable.TT_DOWNLOAD;case "getTaggableName":return "Generated download";}return identity(p,m,a);});
  Recording[] tags={new Recording(1),new Recording(2),new Recording(3),new Recording(4)};for(int i=0;i<2;i++){if((ordinaryMask&(1<<i))!=0)tags[i].members.add(dm);if((savedMask&(1<<i))!=0)tags[i+2].members.add(dm);}
  Type type=(Type)U.allocateInstance(Type.class);set(type,TagTypeBase.class,"tag_type",3);CopyOnWriteList<Tag> list=new CopyOnWriteList<>();for(Recording tag:tags)list.add(tag.tag);set(type,TagTypeWithState.class,"tags",list);
  TagManagerImpl manager=(TagManagerImpl)U.allocateInstance(TagManagerImpl.class);set(null,TagManagerImpl.class,"singleton",manager);CopyOnWriteList<TagTypeBase> types=new CopyOnWriteList<>();types.add(type);set(manager,TagManagerImpl.class,"tag_types",types);Map<Integer,TagTypeBase> byType=new HashMap<>();byType.put(3,type);set(manager,TagManagerImpl.class,"tag_type_map",byType);
  GlobalManager global=(GlobalManager)proxy(GlobalManager.class,(p,m,a)->m.getName().equals("getDownloadManagers")?Collections.singletonList(dm):identity(p,m,a));Core core=(Core)proxy(Core.class,(p,m,a)->m.getName().equals("getGlobalManager")?global:identity(p,m,a));
  TagPropertyUntaggedHandler handler=(TagPropertyUntaggedHandler)U.allocateInstance(TagPropertyUntaggedHandler.class);set(handler,TagPropertyUntaggedHandler.class,"core",core);set(handler,TagPropertyUntaggedHandler.class,"tag_manager",manager);set(handler,TagPropertyUntaggedHandler.class,"is_initialised",true);set(handler,TagPropertyUntaggedHandler.class,"is_enabled",true);Set<Tag> untagged=new HashSet<>();untagged.add(tags[2].tag);untagged.add(tags[3].tag);set(handler,TagPropertyUntaggedHandler.class,"untagged_tags",untagged);Map<Taggable,int[]> counts=new IdentityHashMap<>();set(handler,TagPropertyUntaggedHandler.class,"taggable_counts",counts);
  Method enable=TagPropertyUntaggedHandler.class.getDeclaredMethod("enable");enable.setAccessible(true);enable.invoke(handler);int initial=0;for(int i=0;i<4;i++)if(tags[i].members.contains(dm))initial|=1<<i;
  switch(event){case "add0":tags[0].add(dm);break;case "remove0":tags[0].remove(dm);break;case "add-remove0":tags[0].add(dm);tags[0].remove(dm);break;case "remove-add0":tags[0].remove(dm);tags[0].add(dm);break;case "remove-all":tags[0].remove(dm);tags[1].remove(dm);break;case "disable-reenable-remove0":property(handler,tags[2].tag,false);property(handler,tags[3].tag,false);property(handler,tags[2].tag,true);tags[0].remove(dm);break;case "disable3":
   TagProperty property=(TagProperty)proxy(TagProperty.class,(p,m,a)->m.getName().equals("getTag")?tags[3].tag:m.getName().equals("getBoolean")?false:identity(p,m,a));Method change=TagPropertyUntaggedHandler.class.getDeclaredMethod("handleProperty",TagProperty.class);change.setAccessible(true);change.invoke(handler,property);break;
  }
  int after=0;for(int i=0;i<4;i++)if(tags[i].members.contains(dm))after|=1<<i;Map<String,Object> row=new TreeMap<>();row.put("ordinary_mask",(long)ordinaryMask);row.put("saved_mask",(long)savedMask);row.put("persistent",persistent?1L:0L);row.put("destroyed",destroyed?1L:0L);row.put("event",event);row.put("initial",(long)initial);row.put("after",(long)after);row.put("count",counts.containsKey(dm)?(long)counts.get(dm)[0]:-1L);row.put("untagged_rules",(long)untagged.size());return row;
 }
 public static void main(String[] args)throws Exception{COConfigurationManager.setParameter("tagmanager.enable",false);List<Object> rows=new ArrayList<>();for(int ordinary=0;ordinary<4;ordinary++)for(int saved=0;saved<4;saved++)for(boolean persistent:new boolean[]{false,true})for(boolean destroyed:new boolean[]{false,true})for(String event:new String[]{"none","add0","remove0","add-remove0","remove-add0","remove-all","disable3","disable-reenable-remove0"})rows.add(run(ordinary,saved,persistent,destroyed,event));Map<String,Object> result=new TreeMap<>();result.put("rows",rows);Files.write(Paths.get(args[0]),BEncoder.encode(result));System.out.println("ORIGINAL_UNTAGGED_TAG_EVENTS rows="+rows.size()+" actual_handler_and_listener_bodies=1 PASS");com.biglybt.core.security.SESecurityManager.exitVM(0);}
}
