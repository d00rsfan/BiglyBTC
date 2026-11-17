// SPDX-License-Identifier: GPL-2.0-or-later
package com.biglybt.core.tag.impl;
import com.biglybt.core.config.COConfigurationManager;import com.biglybt.core.tag.*;import com.biglybt.core.util.*;import java.lang.ref.WeakReference;import java.lang.reflect.*;import java.util.*;import java.util.concurrent.atomic.*;
public final class OriginalSuggestionTagRoster {
 public static void main(String[] args)throws Exception{
  COConfigurationManager.initialiseFromMap(new HashMap<>());COConfigurationManager.setParameter("tagmanager.enable",true);
  TagManagerImpl manager=(TagManagerImpl)OriginalOrdinaryTagMembership.U.allocateInstance(TagManagerImpl.class);
  for(Field f:TagManagerImpl.class.getDeclaredFields())if(!Modifier.isStatic(f.getModifiers())){f.setAccessible(true);Class<?> t=f.getType();if(t==CopyOnWriteList.class)f.set(manager,new CopyOnWriteList<>());else if(t==Map.class)f.set(manager,new HashMap<>());else if(t==Set.class)f.set(manager,new HashSet<>());else if(t==List.class)f.set(manager,new ArrayList<>());else if(t==AtomicInteger.class)f.set(manager,new AtomicInteger());else if(t==AtomicLong.class)f.set(manager,new AtomicLong());}
  OriginalOrdinaryTagMembership.set(manager,TagManagerImpl.class,"config_ref",new WeakReference<>(new HashMap<String,Object>()));OriginalOrdinaryTagMembership.set(null,TagManagerImpl.class,"singleton",manager);
  var source=new PinnedSuggestionTagType();if(source.getTagType()!=6||source.getTagCount()!=0||!source.getTags().isEmpty()||source.hasTagTypeFeature(TagFeature.TF_PROPERTIES))throw new AssertionError("Actual suggestion source metadata");var copy=source.getTags();copy.add(null);if(!source.getTags().isEmpty()||source.getTagCount()!=0||!manager.getTagsByName("Generated absent",true).isEmpty())throw new AssertionError("Actual empty source was mutable");System.out.println("ORIGINAL_SUGGESTION_TAG_ROSTER actual_pinned_extracted_methods=1 empty_and_defensive=1 PASS");com.biglybt.core.security.SESecurityManager.exitVM(0);
 }
}
