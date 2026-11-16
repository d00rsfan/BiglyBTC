// SPDX-License-Identifier: GPL-2.0-or-later
package com.biglybt.core.category.impl;
import com.biglybt.core.category.*;
import com.biglybt.core.config.COConfigurationManager;
import com.biglybt.core.download.*;
import com.biglybt.core.internat.MessageText;
import com.biglybt.core.tag.*;
import com.biglybt.core.tag.impl.*;
import com.biglybt.core.util.*;
import java.lang.reflect.*;
import java.nio.file.*;
import java.util.*;
import sun.misc.Unsafe;
/** Original constructors/names/membership, with registration/effect boundaries suppressed. */
public final class OriginalCategoryTags {
 static final Unsafe U;
 static{try{Field f=Unsafe.class.getDeclaredField("theUnsafe");f.setAccessible(true);U=(Unsafe)f.get(null);}catch(Exception e){throw new RuntimeException(e);}}
 static void set(Object value,Class<?> type,String name,Object data)throws Exception{Field f=type.getDeclaredField(name);f.setAccessible(true);f.set(value,data);}
 static Object defaults(Method m){Class<?> t=m.getReturnType();if(t==boolean.class)return false;if(t==int.class)return 0;if(t==long.class)return 0L;return null;}
 static final class RecordingCategory extends CategoryImpl {
  String observedGroup;
  RecordingCategory(CategoryManagerImpl manager,String name,int type){super(manager,name,type,new HashMap<String,String>());}
  RecordingCategory(CategoryManagerImpl manager,String name){super(manager,name,0,0,new HashMap<String,String>());}
  @Override protected void addTag(){}
  @Override public void setGroup(String value){observedGroup=value;}
  @Override public void addTagListener(TagListener listener,boolean fire){}
 }
 @SuppressWarnings("unchecked") public static void main(String[] args)throws Exception{
  COConfigurationManager.setParameter("tagmanager.enable",false);
  set(null,TagManagerImpl.class,"singleton",U.allocateInstance(TagManagerImpl.class));
  MessageText.changeLocale(Locale.ENGLISH);
  CategoryManagerImpl manager=(CategoryManagerImpl)U.allocateInstance(CategoryManagerImpl.class);set(manager,TagTypeBase.class,"tag_type",1);
  List<RecordingCategory> tags=new ArrayList<>();tags.add(new RecordingCategory(manager,"Categories.all",Category.TYPE_ALL));tags.add(new RecordingCategory(manager,"Categories.uncategorized",Category.TYPE_UNCATEGORIZED));
  for(String name:new String[]{"Generated","All","Café","tag.name.product.updates","用户类别"})tags.add(new RecordingCategory(manager,name));
  List<Object> cases=new ArrayList<>();int checks=0;
  for(int assigned=-1;assigned<tags.size();assigned++)for(int member=0;member<2;member++){
   final Category category=assigned<0?null:tags.get(assigned);DownloadManagerState state=(DownloadManagerState)Proxy.newProxyInstance(OriginalCategoryTags.class.getClassLoader(),new Class[]{DownloadManagerState.class},(p,m,a)->m.getName().equals("getCategory")?category:defaults(m));
   DownloadManager download=(DownloadManager)Proxy.newProxyInstance(OriginalCategoryTags.class.getClassLoader(),new Class[]{DownloadManager.class},(p,m,a)->m.getName().equals("getDownloadState")?state:m.getName().equals("hashCode")?System.identityHashCode(p):m.getName().equals("equals")?p==a[0]:defaults(m));
   Taggable other=(Taggable)Proxy.newProxyInstance(OriginalCategoryTags.class.getClassLoader(),new Class[]{Taggable.class},(p,m,a)->m.getName().equals("getTaggableType")?Taggable.TT_PEER:defaults(m));
   List<Object> rows=new ArrayList<>();for(RecordingCategory tag:tags){Field f=CategoryImpl.class.getDeclaredField("managers_set");f.setAccessible(true);Set<DownloadManager> members=(Set<DownloadManager>)f.get(tag);members.clear();if(member!=0&&tag.getType()==Category.TYPE_USER)members.add(download);
    Map<String,Object> row=new TreeMap<>();row.put("id",(long)tag.getTagID());row.put("uid",tag.getTagUID());row.put("kind",(long)tag.getType());row.put("raw",tag.getName());row.put("name",tag.getTagName(true));row.put("group",tag.observedGroup);row.put("member",tag.hasTaggable(download)?1L:0L);row.put("peer",tag.hasTaggable(other)?1L:0L);rows.add(row);checks+=2;}
   Map<String,Object> result=new TreeMap<>();result.put("assigned",(long)assigned);result.put("manual_member",(long)member);result.put("tags",rows);cases.add(result);
  }
  Map<String,Object> out=new TreeMap<>();out.put("cases",cases);Files.write(Paths.get(args[0]),BEncoder.encode(out));
  System.out.println("ORIGINAL_CATEGORY_TAGS cases="+cases.size()+" membership_checks="+checks+" actual_constructors_names_membership=1 PASS");com.biglybt.core.security.SESecurityManager.exitVM(0);
 }
}
