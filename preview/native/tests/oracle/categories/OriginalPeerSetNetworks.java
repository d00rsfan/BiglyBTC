// SPDX-License-Identifier: GPL-2.0-or-later
package com.biglybt.core.speedmanager;
import com.biglybt.core.config.COConfigurationManager;
import com.biglybt.core.util.*;
import com.biglybt.pif.PluginInterface;
import com.biglybt.pif.download.Download;
import com.biglybt.pif.network.ConnectionManager;
import com.biglybt.pif.network.RateLimiter;
import java.lang.reflect.*;
import java.net.*;
import java.nio.file.*;
import java.util.*;
import java.util.regex.Pattern;
/** Actual constructor/token/set parameters/matches; inert plugin/rate factories.
 * No supplied country/interface source or full peer/tag lifecycle. */
public final class OriginalPeerSetNetworks {
 public static void main(String[] args)throws Exception{
  COConfigurationManager.initialiseFromMap(new HashMap<>());COConfigurationManager.setParameter("tagmanager.enable",false);
  Object rate=OriginalPeerSetClassifier.proxy(RateLimiter.class,(p,m,a)->OriginalPeerSetClassifier.primitive(m.getReturnType()));
  Object connection=OriginalPeerSetClassifier.proxy(ConnectionManager.class,(p,m,a)->m.getName().equals("createRateLimiter")?rate:OriginalPeerSetClassifier.primitive(m.getReturnType()));
  Object plugin=OriginalPeerSetClassifier.proxy(PluginInterface.class,(p,m,a)->m.getName().equals("getConnectionManager")?connection:OriginalPeerSetClassifier.primitive(m.getReturnType()));
  SpeedLimitHandler outer=(SpeedLimitHandler)OriginalPeerSetClassifier.U.allocateInstance(SpeedLimitHandler.class);OriginalPeerSetClassifier.set(outer,SpeedLimitHandler.class,"plugin_interface",plugin);
  Class<?> pc=Class.forName(SpeedLimitHandler.class.getName()+"$PeerSet"),tc=Class.forName(pc.getName()+"$TagPeerImpl");Constructor<?> ctor=pc.getDeclaredConstructor(SpeedLimitHandler.class,String.class);ctor.setAccessible(true);Method add=pc.getDeclaredMethod("addCIDRorCCetc",String.class);add.setAccessible(true);Method params=pc.getDeclaredMethod("setParameters",boolean.class,int.class,int.class,int.class,int.class,Set.class,Pattern.class,boolean.class,Pattern.class,boolean.class,Pattern.class,boolean.class,String.class);params.setAccessible(true);Method matches=tc.getDeclaredMethod("matches",Download.class,InetSocketAddress.class);matches.setAccessible(true);
  String[] definitions={"IPv4","IPv6","Public","I2P","Tor","all","IPv4;IPv6","IPv6;Public","203.0.113.0/24;IPv6","203.0.113.0/24;Public"};String[] hosts={"203.0.113.1","198.51.100.1","0.0.0.0","127.0.0.1","2001:db8::1","::1","generated.i2p","generated.onion"};List<Object> rows=new ArrayList<>();
  for(String definition:definitions)for(boolean inverse:new boolean[]{false,true}){
   Object set=ctor.newInstance(outer,"Generated network");boolean accepted=true;for(String token:definition.split(";"))accepted&=(Boolean)add.invoke(set,token);params.invoke(set,inverse,-1,-1,0,0,Collections.emptySet(),null,false,null,false,null,false,null);
   Object tag=OriginalPeerSetClassifier.U.allocateInstance(tc);for(Field f:tc.getDeclaredFields())if(f.isSynthetic()&&f.getType()==pc){f.setAccessible(true);f.set(tag,set);}OriginalPeerSetClassifier.set(tag,tc,"ip_set",set);
   for(String host:hosts){InetSocketAddress address=host.endsWith(".i2p")||host.endsWith(".onion")?InetSocketAddress.createUnresolved(host,12345):new InetSocketAddress(InetAddress.getByName(host),12345);boolean value=(Boolean)matches.invoke(tag,null,address);Map<String,Object> row=new TreeMap<>();row.put("definition",definition);row.put("inverse",inverse?1L:0L);row.put("address",host);row.put("accepted",accepted?1L:0L);row.put("match",value?1L:0L);rows.add(row);}
  }
  Map<String,Object> out=new TreeMap<>();out.put("rows",rows);Files.write(Paths.get(args[0]),BEncoder.encode(out));System.out.println("ORIGINAL_PEER_SET_NETWORKS rows="+rows.size()+" actual_constructor_parameters_match=1 PASS");com.biglybt.core.security.SESecurityManager.exitVM(0);
 }
}
