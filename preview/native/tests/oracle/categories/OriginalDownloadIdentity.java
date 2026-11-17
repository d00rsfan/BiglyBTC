// SPDX-License-Identifier: GPL-2.0-or-later
package com.biglybt.core.tag.impl;
import com.biglybt.core.download.*;
import com.biglybt.core.Core;
import com.biglybt.core.global.GlobalManager;
import com.biglybt.core.util.*;
import java.lang.reflect.*;
import java.nio.file.*;
import java.util.*;
/** Actual stopped DownloadManagerFactory constructor with an inert GM. No Core
 * startup, engine, tracker or peer network is requested. All paths are generated. */
public final class OriginalDownloadIdentity {
 private static Object empty(Class<?> type){
  if(type==boolean.class)return false;
  if(type==byte.class)return (byte)0;if(type==short.class)return (short)0;
  if(type==int.class)return 0;if(type==long.class)return 0L;
  if(type==float.class)return 0f;if(type==double.class)return 0d;
  if(type==char.class)return (char)0;
  if(List.class.isAssignableFrom(type))return Collections.emptyList();
  if(Map.class.isAssignableFrom(type))return Collections.emptyMap();
  return null;
 }
 @SuppressWarnings("unchecked") public static void main(String[] args)throws Exception{
  Path root=Paths.get(args[1]);Files.createDirectories(root);
  Map<String,Object> fixture=BDecoder.decode(Files.readAllBytes(Paths.get(args[0])));
  GlobalManager gm=(GlobalManager)Proxy.newProxyInstance(GlobalManager.class.getClassLoader(),new Class[]{GlobalManager.class},(proxy,method,values)->{
   if(method.getName().equals("equals"))return proxy==values[0];
   if(method.getName().equals("hashCode"))return System.identityHashCode(proxy);
   if(method.getName().equals("toString"))return "Generated inert manager";
   return empty(method.getReturnType());
  });
  Core core=(Core)Proxy.newProxyInstance(Core.class.getClassLoader(),new Class[]{Core.class},(proxy,method,values)->{
   if(method.getName().equals("getGlobalManager"))return gm;
   if(method.getName().equals("equals"))return proxy==values[0];
   if(method.getName().equals("hashCode"))return System.identityHashCode(proxy);
   if(method.getName().equals("toString"))return "Generated inert core";
   return empty(method.getReturnType());
  });
  Field singleton=Class.forName("com.biglybt.core.impl.CoreImpl").getDeclaredField("singleton");singleton.setAccessible(true);singleton.set(null,core);
  int canonical=0,full=0;List<Object> output=new ArrayList<>();
  for(Object entry:(List<?>)fixture.get("rows")){
   Map<String,Object> row=(Map<String,Object>)entry;byte[] info=(byte[])row.get("metainfo");
   List<byte[]> subjects=new ArrayList<>();subjects.add((byte[])row.get("selected"));
   if(row.containsKey("v2"))subjects.add((byte[])row.get("v2"));
   for(byte[] subject:subjects){
    Path area=root.resolve("case-"+(canonical+full));Files.createDirectories(area);
    Path torrent=area.resolve("generated.torrent");Files.write(torrent,info);
    DownloadManager dm=DownloadManagerFactory.create(gm,subject,torrent.toString(),area.resolve("absent-data").toString(),null,
        DownloadManager.STATE_STOPPED,false,false,false,false,null);
    String id=dm.getTaggableID(),error=dm.getErrorDetails();
    if(!Base32.encode(subject).equals(id))throw new AssertionError("Constructor supplied identity encoding differs");
    boolean mismatch=error!=null&&error.contains("Download identity changed");
    if(subject.length==20){if(mismatch)throw new AssertionError("Canonical identity rejected as changed");canonical++;}
    else{if(!mismatch||dm.getTorrent()!=null||dm.getState()!=DownloadManager.STATE_ERROR)throw new AssertionError("Full32 subject not rejected by actual constructor: "+dm.getState()+" "+error);full++;}
    Map<String,Object> result=new TreeMap<>();result.put("runtime",subject);result.put("selected",row.get("selected"));result.put("tag_text",id);result.put("identity_mismatch",mismatch?1L:0L);output.add(result);
   }
  }
  Map<String,Object> encoded=new TreeMap<>();encoded.put("rows",output);Files.write(Paths.get(args[2]),BEncoder.encode(encoded));
  System.out.println("ORIGINAL_DOWNLOAD_IDENTITY canonical="+canonical+" full32_rejected="+full+" actual_factory_constructor=1 inert_core_global_manager_stopped=1 PASS");
  com.biglybt.core.security.SESecurityManager.exitVM(0);
 }
}
