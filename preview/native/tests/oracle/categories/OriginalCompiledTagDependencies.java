// SPDX-License-Identifier: GPL-2.0-or-later
package com.biglybt.core.tag.impl;
import com.biglybt.core.config.COConfigurationManager;
import com.biglybt.core.tag.*;
import com.biglybt.core.util.*;
import java.lang.reflect.*;
import java.nio.file.*;
import java.util.*;
import java.util.concurrent.atomic.AtomicInteger;
import sun.misc.Unsafe;
/** Actual original constraint constructor and dependency set; no expression execution. */
public final class OriginalCompiledTagDependencies {
 static final Unsafe U;static{try{Field f=Unsafe.class.getDeclaredField("theUnsafe");f.setAccessible(true);U=(Unsafe)f.get(null);}catch(Exception e){throw new RuntimeException(e);}}
 static void set(Object o,Class<?> c,String name,Object value)throws Exception{Field f=c.getDeclaredField(name);f.setAccessible(true);f.set(o,value);}
 static Object field(Object o,Class<?> c,String name)throws Exception{Field f=c.getDeclaredField(name);f.setAccessible(true);return f.get(o);}
 static final class RecordingTag extends TagDownloadWithState {String label;Map<String,Object> properties;RecordingTag(){super(null,0,"generated",false,false,false,false,0);}@Override public String getTagName(boolean localized){return label;}@Override public void setTransientProperty(String key,Object value){if(value==null)properties.remove(key);else properties.put(key,value);}@Override public Object getTransientProperty(String key){return properties.get(key);} @Override public boolean isAnyActionEnabled(){return false;}@Override public int getMaximumTaggables(){return 0;}}
 @SuppressWarnings("unchecked") public static void main(String[] args)throws Exception{
  COConfigurationManager.setParameter("tagmanager.enable",false);TagManagerImpl manager=(TagManagerImpl)U.allocateInstance(TagManagerImpl.class);set(null,TagManagerImpl.class,"singleton",manager);set(manager,TagManagerImpl.class,"config_dirty",true);
  TagTypeDownloadManual type=(TagTypeDownloadManual)U.allocateInstance(TagTypeDownloadManual.class);set(type,TagTypeBase.class,"tag_type",3);set(type,TagTypeBase.class,"tag_type_features",TagFeature.TF_PROPERTIES);set(type,TagTypeBase.class,"tags_or_tag_membership_mut",new AtomicInteger());set(type,TagTypeBase.class,"TTP_TAGS_FOR_TAGGABLE_CACHE","generated-cache");CopyOnWriteList<Tag> tags=new CopyOnWriteList<>();set(type,TagTypeWithState.class,"tags",tags);CopyOnWriteList<TagTypeBase> types=new CopyOnWriteList<>();types.add(type);set(manager,TagManagerImpl.class,"tag_types",types);
  String[] labels={"Root","Alpha","Beta","Alpha"};for(int i=0;i<labels.length;i++){RecordingTag tag=(RecordingTag)U.allocateInstance(RecordingTag.class);tag.label=labels[i];tag.properties=new HashMap<>();set(tag,TagBase.class,"tag_id",i);set(tag,TagBase.class,"tag_type",type);tags.add(tag);}
  Class<?> hc=TagPropertyConstraintHandler.class;Object handler=U.allocateInstance(hc);set(handler,hc,"tag_manager",manager);set(handler,hc,"constrained_tags",new HashMap<>());set(handler,hc,"apply_history",new HashMap<>());Class<?> cc=Class.forName(hc.getName()+"$TagConstraint");Constructor<?> ctor=cc.getDeclaredConstructor(hc,Tag.class,String.class,String.class,boolean.class);ctor.setAccessible(true);
  String[] expressions={"1==1","hasTag(\"Alpha\")","hasTagAge(\"Alpha\")>=0","countTag(\"Alpha\")>0","tagPosition(\"Alpha\")>=0","tagPosition(\"Alpha\",1)>=0","hasTagGroup(\"Alpha\")","getTagWeight(\"Alpha\",0)>0","getTagWeight(\"Alpha\",\"type=max\")>0","getTagSort(\"Alpha\")>0","hasTag(\"Beta\")||hasTag(\"Alpha\")","hasTag(\"Alpha\")||hasTag(\"Alpha\")","1==1||hasTag(\"Beta\")","hasTag(\"Missing\")","1==1||hasTag(\"Missing\")","hasTag(\"alpha\")"};
  List<Object> rows=new ArrayList<>();for(String source:expressions){Object rule=ctor.newInstance(handler,tags.get(0),source,"am=3;",true);Object error=((RecordingTag)tags.get(0)).properties.get(Tag.TP_CONSTRAINT_ERROR);boolean valid=field(rule,cc,"expr")!=null;Set<Tag> dependencies=(Set<Tag>)field(rule,cc,"dependent_on_tags");List<Object> ids=new ArrayList<>(),names=new ArrayList<>();if(dependencies!=null){List<Tag> ordered=new ArrayList<>(dependencies);ordered.sort(Comparator.comparingInt(Tag::getTagID));for(Tag tag:ordered){ids.add((long)tag.getTagID());names.add(tag.getTagName(true));}}Map<String,Object> row=new TreeMap<>();row.put("source",source);row.put("valid",valid?1L:0L);row.put("error",error==null?"":error.toString());row.put("ids",ids);row.put("names",names);rows.add(row);}
  Map<String,Object> out=new TreeMap<>();out.put("rows",rows);Files.write(Paths.get(args[0]),BEncoder.encode(out));System.out.println("ORIGINAL_COMPILED_TAG_DEPENDENCIES rows="+rows.size()+" actual_constructor_and_dependency_set=1 PASS");com.biglybt.core.security.SESecurityManager.exitVM(0);
 }
}
