// SPDX-License-Identifier: GPL-2.0-or-later
package com.biglybt.core.speedmanager;
import com.biglybt.core.Core;
import com.biglybt.core.config.COConfigurationManager;
import com.biglybt.core.global.GlobalManager;
import com.biglybt.core.util.*;
import com.biglybt.pif.PluginInterface;
import com.biglybt.pif.network.ConnectionManager;
import com.biglybt.pif.network.RateLimiter;
import java.lang.reflect.*;
import java.nio.file.*;
import java.util.*;
/** Actual complete loadSchedule(false) with generated config, inert Core/GM,
 * limiter factory and check dispatcher. No constructor/Core/tag registration. */
public final class OriginalSavedPeerSets {
 static Object proxy(Class<?> c,InvocationHandler h){return OriginalPeerSetClassifier.proxy(c,h);}
 static Object get(Object o,String field)throws Exception{return OriginalPeerSetClassifier.get(o,o.getClass(),field);}
 static void set(Object o,String field,Object v)throws Exception{OriginalPeerSetClassifier.set(o,SpeedLimitHandler.class,field,v);}
 static String key(String name){return "speed.limit.handler.ipset_n."+Base32.encode(name.getBytes(java.nio.charset.StandardCharsets.UTF_8));}
 static Map<String,Object> run(String name,String[] lines,Map<String,Long> ids)throws Exception{
  for(Object k:new ArrayList<>(COConfigurationManager.getDefinedParameters()))if(k.toString().startsWith("speed.limit.handler."))COConfigurationManager.removeParameter(k.toString());
  COConfigurationManager.setParameter("speed.limit.handler.schedule.lines",Arrays.asList(lines));
  for(Map.Entry<String,Long> id:ids.entrySet())COConfigurationManager.setParameter(key(id.getKey()),id.getValue());
  COConfigurationManager.setParameter("speed.limit.handler.ipset_n.max",100);
  Object global=proxy(GlobalManager.class,(p,m,a)->{throw new AssertionError("Unexpected GM call: "+m.getName());});
  Object core=proxy(Core.class,(p,m,a)->{if(m.getName().equals("getGlobalManager"))return global;throw new AssertionError("Unexpected Core call: "+m.getName());});
  Object connection=proxy(ConnectionManager.class,(p,m,a)->{if(!m.getName().equals("createRateLimiter"))throw new AssertionError(m.getName());final int[] rate={0};return proxy(RateLimiter.class,(r,rm,ra)->{if(rm.getName().equals("setRateLimitBytesPerSecond")){rate[0]=(Integer)ra[0];return null;}if(rm.getName().equals("getRateLimitBytesPerSecond"))return rate[0];return OriginalPeerSetClassifier.primitive(rm.getReturnType());});});
  Object plugin=proxy(PluginInterface.class,(p,m,a)->m.getName().equals("getConnectionManager")?connection:OriginalPeerSetClassifier.primitive(m.getReturnType()));
  SpeedLimitHandler outer=(SpeedLimitHandler)OriginalPeerSetClassifier.U.allocateInstance(SpeedLimitHandler.class);set(outer,"core",core);set(outer,"plugin_interface",plugin);
  for(Field f:SpeedLimitHandler.class.getDeclaredFields())if(!Modifier.isStatic(f.getModifiers())){f.setAccessible(true);if(Map.class.isAssignableFrom(f.getType()))f.set(outer,new HashMap<>());else if(List.class.isAssignableFrom(f.getType()))f.set(outer,new ArrayList<>());else if(Set.class.isAssignableFrom(f.getType()))f.set(outer,new HashSet<>());}
  final int[] checks={0};set(outer,"check_ip_sets_limiter",new FrequencyLimitedDispatcher(null,1){@Override public void dispatch(){checks[0]++;}});
  Method load=SpeedLimitHandler.class.getDeclaredMethod("loadSchedule",boolean.class);load.setAccessible(true);List<?> errors=(List<?>)load.invoke(outer,false);
  Map<String,Object> row=new TreeMap<>();row.put("name",name);row.put("lines",Arrays.asList(lines));row.put("ids",new TreeMap<>(ids));row.put("errors",errors);row.put("enabled",COConfigurationManager.getBooleanParameter("speed.limit.handler.schedule.pa_capable")?2L:(Boolean)get(outer,"is_enabled")?1L:0L);row.put("dispatches",(long)checks[0]);
  List<Object> values=new ArrayList<>();Map<String,Object> ordered=new TreeMap<>((Map)get(outer,"current_ip_sets"));
  for(Map.Entry<String,Object> pair:ordered.entrySet()){
   Object ps=pair.getValue();Map<String,Object> value=new TreeMap<>();value.put("name",pair.getKey());value.put("id",(long)COConfigurationManager.getIntParameter(key(pair.getKey()),-1));value.put("inverse",(Boolean)get(ps,"inverse")?1L:0L);List<Object> ranges=new ArrayList<>();for(long[] range:(long[][])get(ps,"ranges"))ranges.add(Arrays.asList(range[0],range[1]));value.put("ranges",ranges);
   for(String field:new String[]{"country_codes","networks","categories_or_tags"}){Set<?> list=(Set<?>)get(ps,field);value.put(field,list==null?Collections.emptyList():new ArrayList<>(new TreeSet(list)));}
   for(String field:new String[]{"group","client_pattern","intf_pattern","asn_pattern"}){Object v=get(ps,field);value.put(field,v==null?"":v.toString());}
   value.put("up",(long)((RateLimiter)get(ps,"up_limiter")).getRateLimitBytesPerSecond());value.put("down",(long)((RateLimiter)get(ps,"down_limiter")).getRateLimitBytesPerSecond());values.add(value);
  }row.put("sets",values);return row;
 }
 public static void main(String[] args)throws Exception{
  COConfigurationManager.initialiseFromMap(new HashMap<>());COConfigurationManager.setParameter("tagmanager.enable",false);
  String[][] cases={
   {},{" # generated comment"," "},
   {"peer_set Generated=203.0.113.0/24"},
   {"ip_set Generated=203.0.113.0/24;198.51.100.0/24, inverse=yes"},
   {"PEER_SET Generated=203.0.113.0/24, InVeRsE=YES"},
   {"peer_set Generated=203.0.113.0/24, inverse=no"},
   {"peer_set Generated=203.0.113.0/24, inverse=arbitrary"},
   {"peer_set Generated=203.0.113.0/24, inverse=yes, inverse=no"},
   {"peer_set Base=203.0.113.0/24","peer_set Generated=Base 198.51.100.0/24"},
   {"peer_set Generated=Base","peer_set Base=203.0.113.0/24"},
   {"peer_set Generated=203.0.113.0/24","peer_set Generated=198.51.100.0/24, inverse=yes"},
   {"peer_set Generated=203.0.113.0/24, Second=198.51.100.0/24"},
   {"peer_set Generated=203.0.113.0/24,"},
   {"peer_set Generated=203.0.113.0/24,,inverse=yes"},
   {"peer_set Generated=203.0.113.0/24, broken"},
   {"peer_set Generated=203.0.113.0/24, group=Generated group"},
   {"peer_set Generated=203.0.113.0/24, tag=Generated"},
   {"peer_set Generated=203.0.113.0/24, client=Generated"},
   {"peer_set Generated=203.0.113.0/24, intf=Generated"},
   {"peer_set Generated=203.0.113.0/24, asn=Generated"},
   {"peer_set Generated=203.0.113.0/24, up=1k, down=2k"},
   {"peer_set Generated=203.0.113.0/24, peer_up=1k"},
   {"peer_set Generated=203.0.113.0/-1"},
   {"peer_set Generated=203.0.113.0/33"},
   {"peer_set Generated=IPv6"},
   {"peer_set Generated=Public"},
   {"enable=no","peer_set Generated=203.0.113.0/24"},
   {"enable=no","peer_set Generated=203.0.113.0/24","enable=yes"},
   {"enable=bad","peer_set Generated=203.0.113.0/24"},
   {"peer_set Generated=203.0.113.0/24","peer_set Generated=Generated 198.51.100.0/24"},
   {"peer_set Generated=203.0.113.0/24\t198.51.100.0/24"},
   {"peer_set Generated=203.0.113.0/+24"},
   {"peer_set Generated=203.0.113.0/24, inverse=yes="},
   {"peer_set Generated=203.0.113.0/24, inverse="},
   {"peer_set Generated=203.0.113.0/24, client=["},
   {"peer_set Generated=203.0.113.0/24, client=x, intf=y"}
  };
  List<Object> rows=new ArrayList<>();int i=0;for(String[] lines:cases)rows.add(run("case-"+(i++),lines,Map.of("Generated",17L,"Base",18L,"Second",19L)));
  for(long id:new long[]{-1L,-2L,0L,17L,2147483647L,4294967297L})rows.add(run("id-"+id,new String[]{"peer_set Generated=203.0.113.0/24"},Map.of("Generated",id)));
  rows.add(run("clash",new String[]{"peer_set Generated=203.0.113.0/24","peer_set Base=198.51.100.0/24"},Map.of("Generated",17L,"Base",17L)));
  Map<String,Object> out=new TreeMap<>();out.put("rows",rows);Files.write(Paths.get(args[0]),BEncoder.encode(out));System.out.println("ORIGINAL_SAVED_PEER_SETS rows="+rows.size()+" actual_load_schedule=1 inert_core_and_dispatcher=1 PASS");com.biglybt.core.security.SESecurityManager.exitVM(0);
 }
}
