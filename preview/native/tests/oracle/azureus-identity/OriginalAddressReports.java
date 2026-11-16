// SPDX-License-Identifier: GPL-2.0-or-later
package com.biglybt.core.dht.transport.udp.impl;
import java.lang.reflect.*;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.nio.file.*;
import java.util.*;
import com.biglybt.core.dht.DHTLogger;
import com.biglybt.core.dht.transport.DHTTransportContact;
import com.biglybt.core.util.*;
import sun.misc.Unsafe;

/** Actual original gate. Its admitted background task sees address_changing=true
 * and returns before any address discovery/network effect. Deferred timers never
 * reach their30s deadline during this short-lived isolated JVM. */
public final class OriginalAddressReports {
 static long now=10000000;static final Unsafe U;
 static {try{Field f=Unsafe.class.getDeclaredField("theUnsafe");f.setAccessible(true);U=(Unsafe)f.get(null);}catch(Exception e){throw new RuntimeException(e);}}
 static Field field(String name)throws Exception{Field f=DHTTransportUDPImpl.class.getDeclaredField(name);f.setAccessible(true);return f;}
 static void set(Object o,String name,Object value)throws Exception{field(name).set(o,value);}
 static final DHTLogger logger=(DHTLogger)Proxy.newProxyInstance(DHTLogger.class.getClassLoader(),new Class[]{DHTLogger.class},(p,m,a)->m.getReturnType()==boolean.class?false:null);
 static DHTTransportUDPImpl transport(boolean v6,long last,int period,int history,boolean deferred)throws Exception{
  DHTTransportUDPImpl t=(DHTTransportUDPImpl)U.allocateInstance(DHTTransportUDPImpl.class);set(t,"v6",v6);set(t,"external_address",v6?"2001:db8:0:0:0:0:0:1":"198.51.100.1");set(t,"this_mon",new AEMonitor("generated-address-policy"));set(t,"logger",logger);set(t,"last_address_change",last);set(t,"min_address_change_period",period);set(t,"initial_address_change_deferred",deferred);set(t,"address_changing",true);Map<InetSocketAddress,DHTTransportContact> contacts=new LinkedHashMap<>();for(int i=0;i<history;i++)contacts.put(new InetSocketAddress("127.0.0.1",10000+i),null);set(t,"contact_history",contacts);return t;
 }
 static DHTTransportUDPContactImpl reporter(boolean v6)throws Exception{DHTTransportUDPContactImpl c=(DHTTransportUDPContactImpl)U.allocateInstance(DHTTransportUDPContactImpl.class);Field f=DHTTransportUDPContactImpl.class.getDeclaredField("external_address");f.setAccessible(true);InetSocketAddress endpoint=new InetSocketAddress(v6?"2001:db8::2":"198.51.100.2",4444);f.set(c,endpoint);for(String name:new String[]{"transport_address","id","protocol_version"}){Field member=DHTTransportUDPContactImpl.class.getDeclaredField(name);member.setAccessible(true);member.set(c,name.equals("id")?new byte[20]:name.equals("protocol_version")?(byte)55:endpoint);}return c;}
 public static void main(String[] args)throws Exception{
  SystemTime.getCurrentTime();Field clockField=SystemTime.class.getDeclaredField("instance");clockField.setAccessible(true);Object old=clockField.get(null);Class<?> type=Class.forName("com.biglybt.core.util.SystemTime$SystemTimeProvider");Object clock=Proxy.newProxyInstance(type.getClassLoader(),new Class[]{type},(p,m,a)->now);List<String> rows=new ArrayList<>();
  try{
   U.putObjectVolatile(U.staticFieldBase(clockField),U.staticFieldOffset(clockField),clock);
   for(boolean v6:new boolean[]{false,true})for(int history:new int[]{0,31,32})for(boolean force:new boolean[]{false,true})for(boolean deferred:new boolean[]{false,true}){
    String[] addresses=v6?new String[]{"2001:db8::1","2001:db8::2","2001:db8::3","::1","fe80::1","fec0::1","fc00::1","ff02::1","198.51.100.3"}:new String[]{"198.51.100.1","198.51.100.2","203.0.113.3","127.0.0.1","169.254.1.2","192.168.1.2","0.0.0.0","224.0.0.1","2001:db8::3"};
    for(String address:addresses){now=10000000;DHTTransportUDPImpl t=transport(v6,0,300000,history,deferred);t.externalAddressChange(reporter(v6),new InetSocketAddress(address,5555),force);rows.add(String.join("\t",v6?"6":"4",address,Long.toString(now),"0","300000",Integer.toString(history),force?"1":"0",deferred?"1":"0",Long.toString(field("last_address_change").getLong(t)),Integer.toString(field("min_address_change_period").getInt(t)),field("initial_address_change_deferred").getBoolean(t)?"1":"0"));}
   }
   for(long[] times:new long[][]{{1000,1001},{301000,1000},{300999,1000},{301001,1000},{Long.MIN_VALUE,Long.MAX_VALUE},{Long.MAX_VALUE,Long.MIN_VALUE},{601000,1000}})for(int period:new int[]{300000,600000}){now=times[0];DHTTransportUDPImpl t=transport(false,times[1],period,32,false);t.externalAddressChange(reporter(false),new InetSocketAddress("203.0.113.3",5555),false);rows.add(String.join("\t","4","203.0.113.3",Long.toString(now),Long.toString(times[1]),Integer.toString(period),"32","0","0",Long.toString(field("last_address_change").getLong(t)),Integer.toString(field("min_address_change_period").getInt(t)),field("initial_address_change_deferred").getBoolean(t)?"1":"0"));}
   Files.write(Paths.get(args[0]),rows);System.out.println("ORIGINAL_ADDRESS_REPORTS actual_gate_cases="+rows.size()+" discovery_threads_suppressed=1 PASS");
  }finally{U.putObjectVolatile(U.staticFieldBase(clockField),U.staticFieldOffset(clockField),old);}
  com.biglybt.core.security.SESecurityManager.exitVM(0);
 }
}
