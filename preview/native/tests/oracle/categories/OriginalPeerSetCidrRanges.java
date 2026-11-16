// SPDX-License-Identifier: GPL-2.0-or-later
package com.biglybt.core.speedmanager;
import com.biglybt.core.config.COConfigurationManager;
import com.biglybt.core.util.BEncoder;
import com.biglybt.pif.PluginInterface;
import com.biglybt.pif.download.Download;
import com.biglybt.pif.network.ConnectionManager;
import com.biglybt.pif.network.RateLimiter;
import java.lang.reflect.*;
import java.net.*;
import java.nio.file.*;
import java.util.*;
import java.util.regex.Pattern;
import sun.misc.Unsafe;
/** Actual PeerSet constructor/parser/parameters and TagPeerImpl.matches.
 * Outer services and limiter factory are inert; tag impl allocation supplies
 * its enclosing set only, without registering tags or constructing a Core. */
public final class OriginalPeerSetCidrRanges {
 static final Unsafe U;static{try{Field f=Unsafe.class.getDeclaredField("theUnsafe");f.setAccessible(true);U=(Unsafe)f.get(null);}catch(Exception e){throw new RuntimeException(e);}}
 static void set(Object o,Class<?> c,String name,Object value)throws Exception{Field f=c.getDeclaredField(name);f.setAccessible(true);f.set(o,value);}
 static Object get(Object o,Class<?> c,String name)throws Exception{Field f=c.getDeclaredField(name);f.setAccessible(true);return f.get(o);}
 static Object primitive(Class<?> c){if(c==boolean.class)return false;if(c==int.class)return 0;if(c==long.class)return 0L;if(c==float.class)return 0F;if(c==double.class)return 0D;if(c==byte.class)return (byte)0;if(c==short.class)return (short)0;if(c==char.class)return (char)0;return null;}
 static Object proxy(Class<?> c,InvocationHandler h){return java.lang.reflect.Proxy.newProxyInstance(c.getClassLoader(),new Class[]{c},h);}
 public static void main(String[] args)throws Exception{
  COConfigurationManager.setParameter("tagmanager.enable",false);
  Object rate=proxy(RateLimiter.class,(p,m,a)->primitive(m.getReturnType()));Object connections=proxy(ConnectionManager.class,(p,m,a)->m.getName().equals("createRateLimiter")?rate:primitive(m.getReturnType()));Object plugin=proxy(PluginInterface.class,(p,m,a)->m.getName().equals("getConnectionManager")?connections:primitive(m.getReturnType()));
  SpeedLimitHandler outer=(SpeedLimitHandler)U.allocateInstance(SpeedLimitHandler.class);set(outer,SpeedLimitHandler.class,"plugin_interface",plugin);
  Class<?> pc=Class.forName(SpeedLimitHandler.class.getName()+"$PeerSet"),tc=Class.forName(pc.getName()+"$TagPeerImpl");Constructor<?> ctor=pc.getDeclaredConstructor(SpeedLimitHandler.class,String.class);ctor.setAccessible(true);Method add=pc.getDeclaredMethod("addCIDRorCCetc",String.class);add.setAccessible(true);Method parameters=pc.getDeclaredMethod("setParameters",boolean.class,int.class,int.class,int.class,int.class,Set.class,Pattern.class,boolean.class,Pattern.class,boolean.class,Pattern.class,boolean.class,String.class);parameters.setAccessible(true);Method matches=tc.getDeclaredMethod("matches",Download.class,InetSocketAddress.class);matches.setAccessible(true);
  String[] tokens={"203.0.113.0/24","203.0.113.7/32","0.0.0.0/0","203.0.113.0/-1","203.0.113.0/33","2001:db8::/32","203.0.113.0/24;203.0.113.0/24","203.0.113.0/24;203.0.114.0/24","203.0.113.0/24;203.0.113.7/32"};String[] hosts={"203.0.113.1","203.0.114.1","203.0.113.7","0.0.0.0","2001:db8::1","generated.i2p","generated.onion"};List<Object> rows=new ArrayList<>();
  for(String token:tokens)for(boolean inverse:new boolean[]{false,true})for(boolean needDownload:new boolean[]{false,true}){
   Object set=ctor.newInstance(outer,"Generated");boolean accepted=true;for(String part:token.split(";")){accepted &= (Boolean)add.invoke(set,part);}Set<String> categories=needDownload?Collections.singleton("Generated category"):Collections.emptySet();parameters.invoke(set,inverse,-1,-1,0,0,categories,null,false,null,false,null,false,null);
   Object tag=U.allocateInstance(tc);for(Field f:tc.getDeclaredFields())if(f.isSynthetic()&&f.getType()==pc){f.setAccessible(true);f.set(tag,set);}set(tag,tc,"ip_set",set);
   for(String host:hosts){InetSocketAddress address=host.endsWith(".i2p")||host.endsWith(".onion")?InetSocketAddress.createUnresolved(host,12345):new InetSocketAddress(InetAddress.getByName(host),12345);boolean value=(Boolean)matches.invoke(tag,null,address);Map<String,Object> row=new TreeMap<>();row.put("token",token);row.put("inverse",inverse?1L:0L);row.put("needs_download",needDownload?1L:0L);row.put("address",host);row.put("accepted",accepted?1L:0L);row.put("match",value?1L:0L);List<Object> ranges=new ArrayList<>();for(long[] range:(long[][])get(set,pc,"ranges"))ranges.add(Arrays.asList(range[0],range[1]));row.put("ranges",ranges);rows.add(row);}
  }
  Map<String,Object> out=new TreeMap<>();out.put("rows",rows);Files.write(Paths.get(args[0]),BEncoder.encode(out));System.out.println("ORIGINAL_PEER_SET_CIDR_RANGES rows="+rows.size()+" actual_constructor_token_parameters_and_matches=1 PASS");com.biglybt.core.security.SESecurityManager.exitVM(0);
 }
}
