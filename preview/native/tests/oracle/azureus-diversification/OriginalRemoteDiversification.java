// SPDX-License-Identifier: GPL-2.0-or-later
package com.biglybt.plugin.dht.impl;
import com.biglybt.core.dht.DHTLogger;
import com.biglybt.core.util.*;
import java.lang.reflect.*;
import java.util.*;
import java.nio.file.*;
public final class OriginalRemoteDiversification {
 static void need(boolean b){if(!b)throw new AssertionError();}
 static List<byte[]> keys(List input){List<byte[]> result=new ArrayList<byte[]>();for(Object o:input)result.add(((HashWrapper)o).getBytes());return result;}
 public static void main(String[] args)throws Exception{
  Field unsafe=sun.misc.Unsafe.class.getDeclaredField("theUnsafe");unsafe.setAccessible(true);sun.misc.Unsafe allocator=(sun.misc.Unsafe)unsafe.get(null);
  DHTPluginStorageManager manager=(DHTPluginStorageManager)allocator.allocateInstance(DHTPluginStorageManager.class);
  Field logger=DHTPluginStorageManager.class.getDeclaredField("log");logger.setAccessible(true);logger.set(manager,Proxy.newProxyInstance(DHTLogger.class.getClassLoader(),new Class[]{DHTLogger.class},(p,m,a)->m.getReturnType()==boolean.class?Boolean.FALSE:null));
  Field remote=DHTPluginStorageManager.class.getDeclaredField("remote_diversifications");remote.setAccessible(true);Map ledger=new HashMap();remote.set(manager,ledger);
  List<Map> cases=new ArrayList<Map>();
  for(int n=0;n<32;n++){
   byte[] key=new byte[20];for(int i=0;i<20;i++)key[i]=(byte)(n*23+i);int type=2+n%2;long expiry=2000000000000L+n;
   Map row=new HashMap();row.put("key",key);row.put("type",(long)type);row.put("exp",expiry);row.put("fpo",Arrays.asList((long)(n%10),(long)((n+3)%10)));
   DHTPluginStorageManager.diversification d=DHTPluginStorageManager.diversification.deserialise(manager,row);need(d.getExpiry()==expiry&&d.getType()==type);Map serialized=d.serialise();
   Map out=new HashMap();out.put("record",serialized);out.put("put",keys(d.getKeys(true,false)));out.put("put_exhaustive",keys(d.getKeys(true,true)));out.put("get",keys(d.getKeys(false,false)));out.put("get_exhaustive",keys(d.getKeys(false,true)));cases.add(out);
   ledger.put(new HashWrapper(key),d);need(manager.lookupDiversification(new HashWrapper(key))==d);
  }
  byte[] expired=new byte[20];Arrays.fill(expired,(byte)99);Map old=new HashMap();old.put("key",expired);old.put("type",3L);old.put("exp",SystemTime.getCurrentTime()-10000);old.put("fpo",Arrays.asList(1L,7L));ledger.put(new HashWrapper(expired),DHTPluginStorageManager.diversification.deserialise(manager,old));need(manager.lookupDiversification(new HashWrapper(expired))==null&&!ledger.containsKey(new HashWrapper(expired)));
  long before=SystemTime.getCurrentTime();DHTPluginStorageManager.diversification minted=new DHTPluginStorageManager.diversification(manager,new HashWrapper(expired),(byte)3);long after=SystemTime.getCurrentTime();Map fresh=minted.serialise();long exp=(Long)fresh.get("exp");need(exp>=before+172800000L&&exp<after+259200000L);List offsets=(List)fresh.get("fpo");need(offsets.size()==2&&!offsets.get(0).equals(offsets.get(1)));
  Map result=new HashMap();result.put("cases",cases);result.put("expired_removed",1L);result.put("minted_ttl_and_offsets",1L);Files.write(Paths.get(args[0]),BEncoder.encode(result));System.out.println("Actual original diversification methods: "+cases.size()+" cases; expiry and minted TTL/offsets PASS");
 }
}
