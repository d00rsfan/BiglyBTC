// SPDX-License-Identifier: GPL-2.0-or-later
package com.biglybt.core.tag.impl;
import com.biglybt.core.tag.*;
import com.biglybt.core.util.*;
import java.lang.reflect.*;
import java.nio.file.*;
import java.util.*;
/** Actual constraint constructor; supplied family/feature/name registration
 * boundaries only, not a running global Core or tag membership service. */
public final class OriginalConstraintNameCensus {
 @SuppressWarnings("unchecked") static Map<String,Object> run(int mask,boolean duplicate)throws Exception{
  var manager=(TagManagerImpl)OriginalOrdinaryTagMembership.U.allocateInstance(TagManagerImpl.class);
  OriginalOrdinaryTagMembership.set(null,TagManagerImpl.class,"singleton",manager);
  CopyOnWriteList<TagTypeBase> types=new CopyOnWriteList<>();
  OriginalOrdinaryTagMembership.set(manager,TagManagerImpl.class,"tag_types",types);
  var rootType=OriginalPeerSetConstraintFacts.type(3);
  var root=OriginalPeerSetConstraintFacts.tag(rootType,0);
  Field roster=TagTypeWithState.class.getDeclaredField("tags");roster.setAccessible(true);
  for(int id=1;id<=7;id++)if((mask&(1<<(id-1)))!=0){
   var type=OriginalPeerSetConstraintFacts.type(id);
   OriginalOrdinaryTagMembership.set(type,TagTypeBase.class,"tag_type_features",id==1||id==2||id==3||id==5?TagFeature.TF_PROPERTIES:0);
   types.add(type);CopyOnWriteList<Tag> tags=(CopyOnWriteList<Tag>)roster.get(type);
   for(int n=1;n<=(duplicate?2:1);n++){
    var target=OriginalPeerSetConstraintFacts.tag(type,n);
    Tag exposed=(Tag)Proxy.newProxyInstance(OriginalConstraintNameCensus.class.getClassLoader(),new Class[]{Tag.class},(p,m,a)->m.getName().equals("getTagName")?"Generated symbol":m.invoke(target,a));tags.add(exposed);
   }
  }
  var handler=OriginalOrdinaryTagMembership.U.allocateInstance(TagPropertyConstraintHandler.class);
  OriginalOrdinaryTagMembership.set(handler,TagPropertyConstraintHandler.class,"tag_manager",manager);
  OriginalOrdinaryTagMembership.set(handler,TagPropertyConstraintHandler.class,"constrained_tags",new HashMap<>());
  OriginalOrdinaryTagMembership.set(handler,TagPropertyConstraintHandler.class,"apply_history",new HashMap<>());
  Class<?> c=OriginalOrdinaryTagMembership.CONSTRAINT;
  Constructor<?> ctor=c.getDeclaredConstructor(TagPropertyConstraintHandler.class,Tag.class,String.class,String.class,boolean.class);ctor.setAccessible(true);
  Object rule=ctor.newInstance(handler,root,"countTag(\"Generated symbol\")>=0","am=0;",true);
  Field body=c.getDeclaredField("expr");body.setAccessible(true);if(body.get(rule)==null)throw new AssertionError("Actual constructor failed");
  Field deps=c.getDeclaredField("dependent_on_tags");deps.setAccessible(true);
  List<Long> ids=new ArrayList<>();Set<Tag> selected=(Set<Tag>)deps.get(rule);if(selected!=null)for(Tag tag:selected)ids.add(tag.getTagUID());Collections.sort(ids);
  Map<String,Object> row=new TreeMap<>();row.put("mask",(long)mask);row.put("duplicate",duplicate?1L:0L);row.put("dependent_uids",ids);return row;
 }
 public static void main(String[] args)throws Exception{
  List<Object> rows=new ArrayList<>();for(int mask=1;mask<128;mask++)for(boolean duplicate:new boolean[]{false,true})rows.add(run(mask,duplicate));
  Map<String,Object> out=new TreeMap<>();out.put("rows",rows);Files.write(Paths.get(args[0]),BEncoder.encode(out));
  System.out.println("ORIGINAL_CONSTRAINT_NAME_CENSUS rows="+rows.size()+" actual_constructor=1 supplied_family_feature_name_boundaries=1 PASS");com.biglybt.core.security.SESecurityManager.exitVM(0);
 }
}
