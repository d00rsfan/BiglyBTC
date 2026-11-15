// SPDX-License-Identifier: GPL-2.0-or-later
package com.biglybt.plugin.dht.impl;
import java.lang.reflect.*;
import java.net.InetAddress;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;
import com.biglybt.core.dht.DHTLogger;
import com.biglybt.core.util.*;
import sun.misc.Unsafe;

/** Actual original address-cache methods, isolated files and controlled clock. */
public final class OriginalRecentAddresses {
 static long now=1000000000000L;static final long WEEK=604800000L;
 static final DHTLogger logger=(DHTLogger)Proxy.newProxyInstance(DHTLogger.class.getClassLoader(),new Class[]{DHTLogger.class},(p,m,a)->m.getReturnType()==boolean.class?false:null);
 static String hex(byte[] bytes){StringBuilder out=new StringBuilder();for(byte b:bytes)out.append(String.format("%02x",b&255));return out.toString();}
 static String text(String value){return value==null?"-":hex(value.getBytes(StandardCharsets.UTF_8));}
 public static void main(String[] args)throws Exception{
  SystemTime.getCurrentTime();Field uf=Unsafe.class.getDeclaredField("theUnsafe");uf.setAccessible(true);Unsafe u=(Unsafe)uf.get(null);Field f=SystemTime.class.getDeclaredField("instance");f.setAccessible(true);Object old=f.get(null);Class<?> type=Class.forName("com.biglybt.core.util.SystemTime$SystemTimeProvider");Object clock=Proxy.newProxyInstance(type.getClassLoader(),new Class[]{type},(p,m,a)->now);Path base=Paths.get(args[0]);Files.createDirectories(base);Path root=Files.createTempDirectory(base,"owned-");List<String> rows=new ArrayList<>();int count=0;
  try{
   u.putObjectVolatile(u.staticFieldBase(f),u.staticFieldOffset(f),clock);
   for(int shape=0;shape<6;shape++)for(String operation:new String[]{"load","new","same","reserved"}){
    now=shape==4?Long.MAX_VALUE-1000:1000000000000L;Map<String,Object> input=new HashMap<>();if(shape!=0){input.put("127.1.2.1",now-WEEK-1);input.put("127.1.2.2",now-WEEK);input.put("127.1.2.3",now-WEEK+1);input.put("127.1.2.4",now+5000);input.put("counter",Long.MIN_VALUE);if(shape!=2)input.put("most_recent","127.1.2.1".getBytes(StandardCharsets.UTF_8));}if(shape==5)input.put("most_recent","2001:db8:0:0:0:0:0:1".getBytes(StandardCharsets.UTF_8));
    byte[] before=BEncoder.encode(input);Path dir=root.resolve("case-"+count);Files.createDirectories(dir);Files.write(dir.resolve(shape==3?"addresses.saving":"addresses.dat"),before);DHTPluginStorageManager manager=new DHTPluginStorageManager(0,logger,dir.toFile());String address=operation.equals("same")?"127.1.2.1":operation.equals("reserved")?"most_recent":"127.2.3.4";
    if(!operation.equals("load"))manager.recordCurrentAddress(address);byte[] after=operation.equals("load")?before:Files.readAllBytes(dir.resolve("addresses.dat"));rows.add(String.join("\t","case-"+count,operation,Long.toString(now),hex(before),text(address),text(manager.getMostRecentAddress()),manager.isRecentAddress("127.1.2.1")?"1":"0",manager.isRecentAddress(address)?"1":"0",hex(after)));count++;
   }
   for(String input:new String[]{"127.0.0.1","2001:db8::1","2001:0db8:00ab::00ff","::1","::ffff:192.0.2.1","2001:db8::"})rows.add("format\t"+text(input)+"\t"+text(InetAddress.getByName(input).getHostAddress()));
   Files.write(Paths.get(args[1]),rows);System.out.println("ORIGINAL_RECENT_ADDRESSES cases="+count+" actual_host_formats=6 fixed_clock=1 PASS");
  }finally{u.putObjectVolatile(u.staticFieldBase(f),u.staticFieldOffset(f),old);try(java.util.stream.Stream<Path> paths=Files.walk(root)){for(Path p:(Iterable<Path>)paths.sorted(Comparator.reverseOrder())::iterator)Files.delete(p);}}
  com.biglybt.core.security.SESecurityManager.exitVM(0);
 }
}
