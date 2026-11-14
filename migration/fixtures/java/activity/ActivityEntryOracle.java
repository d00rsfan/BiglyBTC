// SPDX-License-Identifier: GPL-2.0-or-later
package com.biglybt.activities;
import java.nio.file.*;
import java.util.*;
import com.biglybt.core.util.BEncoder;
import com.biglybt.plugin.net.buddy.BuddyPluginBeta;
public final class ActivityEntryOracle {
 public static void main(String[] args)throws Exception {
  Map<String,String> callback=new HashMap<>();callback.put("allowReAdd","true");callback.put("net","Public");callback.put("key","Generated notification room");
  LocalActivityManager.addLocalActivity("Public: Generated notification room","image.sidebar.chat-overview","Generated notification room: Generated notification text",new String[]{"View"},BuddyPluginBeta.ActivityCallback.class,callback);
  if(LocalActivityManager.pending==null||LocalActivityManager.pending.size()!=1){throw new AssertionError("Generated pending activity");}
  ActivitiesEntry entry=LocalActivityManager.pending.get(0);entry.setTimestamp(1760000000123L);
  Files.write(Paths.get(args[0]),BEncoder.encode(entry.toMap()));
  System.out.println("ORIGINAL_ACTIVITY_FACTORY_MAP_OK");
 }
}
