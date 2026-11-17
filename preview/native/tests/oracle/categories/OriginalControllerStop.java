// SPDX-License-Identifier: GPL-2.0-or-later
package com.biglybt.core.tag.impl;
import com.biglybt.core.Core;
import com.biglybt.core.download.*;
import com.biglybt.core.global.GlobalManager;
import com.biglybt.core.util.*;
import java.lang.reflect.*;
import java.nio.file.*;
import java.util.*;
/** Actual controller stopIt on normally constructed stopped download objects.
 * Initial state/force are supplied reflectively;disk/peer managers stay absent.
 * Core/GM are inert;this is not real Java transfer/retirement qualification. */
public final class OriginalControllerStop {
 static Object empty(Class<?> type){if(type==boolean.class)return false;if(type==int.class)return 0;if(type==long.class)return 0L;if(List.class.isAssignableFrom(type))return Collections.emptyList();if(Map.class.isAssignableFrom(type))return Collections.emptyMap();return null;}
 static void set(Object value,Class<?> owner,String name,Object data)throws Exception{Field f=owner.getDeclaredField(name);f.setAccessible(true);f.set(value,data);}
 @SuppressWarnings("unchecked") public static void main(String[] args)throws Exception{
  GlobalManager gm=(GlobalManager)Proxy.newProxyInstance(GlobalManager.class.getClassLoader(),new Class[]{GlobalManager.class},(p,m,a)->{if(m.getName().equals("hashCode"))return System.identityHashCode(p);if(m.getName().equals("equals"))return p==a[0];return empty(m.getReturnType());});
  Core core=(Core)Proxy.newProxyInstance(Core.class.getClassLoader(),new Class[]{Core.class},(p,m,a)->m.getName().equals("getGlobalManager")?gm:empty(m.getReturnType()));
  set(null,Class.forName("com.biglybt.core.impl.CoreImpl"),"singleton",core);
  Map<String,Object> fixture=BDecoder.decode(Files.readAllBytes(Paths.get(args[0])));Map<String,Object> sample=(Map<String,Object>)((List<?>)fixture.get("rows")).get(0);
  Path work=Paths.get(args[1]);Files.createDirectories(work);List<Object> rows=new ArrayList<>();
  Class<?> implementation=Class.forName("com.biglybt.core.download.impl.DownloadManagerImpl"),controller=Class.forName("com.biglybt.core.download.impl.DownloadManagerController");Field controllerField=implementation.getDeclaredField("controller");controllerField.setAccessible(true);Method stop=controller.getDeclaredMethod("stopIt",int.class,boolean.class,boolean.class,boolean.class);stop.setAccessible(true);
  for(int state:new int[]{0,10,30,50,60,65,70,75,100})for(int force=0;force<2;force++)for(int target:new int[]{70,75}){
   Path area=work.resolve("case-"+rows.size());Files.createDirectories(area);Path torrent=area.resolve("generated.torrent");Files.write(torrent,(byte[])sample.get("metainfo"));
   DownloadManager dm=DownloadManagerFactory.create(gm,(byte[])sample.get("selected"),torrent.toString(),area.resolve("absent-data").toString(),null,DownloadManager.STATE_STOPPED,false,false,false,false,null);
   Object actual=controllerField.get(dm);set(actual,controller,"state_set_by_method",state);set(actual,controller,"force_start",force!=0);
   stop.invoke(actual,target,false,false,false);
   Map<String,Object> row=new TreeMap<>();row.put("state",(long)state);row.put("force",(long)force);row.put("target",(long)target);row.put("after_state",(long)dm.getState());row.put("after_force",dm.isForceStart()?1L:0L);set(actual,controller,"state_set_by_method",state);set(actual,controller,"force_start",force!=0);dm.setStateQueued();row.put("queue_state",(long)dm.getState());row.put("queue_force",dm.isForceStart()?1L:0L);rows.add(row);
  }
  Files.write(Paths.get(args[2]),BEncoder.encode(Collections.<String,Object>singletonMap("rows",rows)));
  System.out.println("ORIGINAL_CONTROLLER_STOP rows="+rows.size()+" actual_stop_method=1 supplied_initial_state_force_absent_disk_peer=1 PASS");com.biglybt.core.security.SESecurityManager.exitVM(0);
 }
}
