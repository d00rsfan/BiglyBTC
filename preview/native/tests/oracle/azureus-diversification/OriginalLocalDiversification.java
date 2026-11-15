// SPDX-License-Identifier: GPL-2.0-or-later
package com.biglybt.plugin.dht.impl;
import com.biglybt.core.dht.DHTLogger;
import com.biglybt.core.util.*;
import java.lang.reflect.*;
import java.util.*;
import java.nio.file.*;
import java.io.*;
public final class OriginalLocalDiversification {
 static final class Manager extends DHTPluginStorageManager {
  int writes;Manager(){super(0,null,null);}
  protected void writeDiversifications(){writes++;}
 }
 public static void main(String[] args)throws Exception{
  Field u=sun.misc.Unsafe.class.getDeclaredField("theUnsafe");u.setAccessible(true);sun.misc.Unsafe allocator=(sun.misc.Unsafe)u.get(null);Manager m=(Manager)allocator.allocateInstance(Manager.class);
  Field log=DHTPluginStorageManager.class.getDeclaredField("log");log.setAccessible(true);log.set(m,Proxy.newProxyInstance(DHTLogger.class.getClassLoader(),new Class[]{DHTLogger.class},(p,f,a)->f.getReturnType()==boolean.class?Boolean.FALSE:null));
  if(SystemTime.getCurrentTime()<1000000||SystemTime.getCurrentTime()>=2000000000000L)throw new AssertionError("Fixture expiry window");
  List<Map> cases=new ArrayList<Map>();for(int n=0;n<18;n++){
   byte[] key=new byte[20];for(int i=0;i<20;i++)key[i]=(byte)(31*n+i);Map row=new HashMap();row.put("key",key);row.put("type",(long)(1+n%3));row.put("exp",n%5==0?1L:2000000000000L+n);
   DHTPluginStorageManager.storageKey k=DHTPluginStorageManager.storageKey.deserialise(m,row);Map before=k.serialise();m.writes=0;ByteArrayOutputStream bytes=new ByteArrayOutputStream();k.serialiseStats(new DataOutputStream(bytes));Map after=k.serialise();
   Map result=new HashMap();result.put("before",before);result.put("after",after);result.put("stats",bytes.toByteArray());result.put("writes",(long)m.writes);cases.add(result);
  }
  Map fixture=new HashMap();fixture.put("cases",cases);fixture.put("comparison_now",1000000L);Files.write(Paths.get(args[0]),BEncoder.encode(fixture));System.out.println("Original local storage checkpoints: "+cases.size()+" cases");
 }
}
