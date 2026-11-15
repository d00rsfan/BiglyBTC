// SPDX-License-Identifier: GPL-2.0-or-later
package com.biglybt.plugin.dht.impl;
import java.io.*;
import java.lang.reflect.*;
import java.nio.file.*;
import java.security.*;
import java.security.interfaces.RSAPublicKey;
import java.util.*;
import com.biglybt.core.dht.*;
import com.biglybt.core.dht.transport.DHTTransportContact;
import com.biglybt.core.util.*;

/** Actual original manager, generated directory and temporary in-JVM test key. */
public final class OriginalKeyBlockPolicy {
 static final List<String> rows=new ArrayList<>();static Path root;static KeyPair pair;static int calls;
 static final DHTLogger logger=(DHTLogger)Proxy.newProxyInstance(DHTLogger.class.getClassLoader(),new Class[]{DHTLogger.class},(p,m,a)->m.getReturnType()==boolean.class?false:null);
 static Field field(String name)throws Exception{Field f=DHTPluginStorageManager.class.getDeclaredField(name);f.setAccessible(true);return f;}
 static String hex(byte[] bytes){StringBuilder s=new StringBuilder();for(byte b:bytes)s.append(String.format("%02x",b&255));return s.toString();}
 static byte[] request(int flag,int created){byte[] r=new byte[28];r[0]=(byte)flag;r[1]=7;r[4]=(byte)(created>>>24);r[5]=(byte)(created>>>16);r[6]=(byte)(created>>>8);r[7]=(byte)created;Arrays.fill(r,8,28,(byte)83);return r;}
 static byte[] sign(byte[] request)throws Exception{Signature s=Signature.getInstance("MD5withRSA");s.initSign(pair.getPrivate());s.update(request);return s.sign();}
 static DHTTransportContact contact(final int index){return (DHTTransportContact)Proxy.newProxyInstance(DHTTransportContact.class.getClassLoader(),new Class[]{DHTTransportContact.class},(p,m,a)->{if(m.getName().equals("getID")){byte[] id=new byte[20];id[0]=(byte)index;return id;}if(m.getReturnType()==boolean.class)return false;if(m.getReturnType()==int.class)return 0;if(m.getReturnType()==byte.class)return (byte)55;return null;});}
 static DHTPluginStorageManager manager(String name)throws Exception{Path dir=root.resolve(name);Files.createDirectories(dir);return new DHTPluginStorageManager(0,logger,dir.toFile());}
 static DHTPluginStorageManager.keyBlock stored(DHTPluginStorageManager manager){return (DHTPluginStorageManager.keyBlock)((ByteArrayHashMap)get(manager,"key_block_map_cow")).get(Arrays.copyOfRange(request(1,0),8,28));}
 static Object get(Object o,String name){try{return field(name).get(o);}catch(Exception e){throw new RuntimeException(e);}}
 static void operation(String scenario,DHTPluginStorageManager manager,boolean direct,int flag,int created,boolean valid)throws Exception{
  byte[] r=request(flag,created),sig=sign(r);if(!valid)sig[0]^=1;
  // Separate pure precedence decisions from the independently tested fail filter.
  field("kb_verify_fail_bloom").set(manager,null);
  DHTStorageBlock result=manager.keyBlockRequest(direct?contact(1):null,r,sig);DHTPluginStorageManager.keyBlock old=stored(manager);
  rows.add(String.join("\t",scenario,"operation",direct?"1":"0",Integer.toString(flag),Integer.toString(created),hex(r),hex(sig),result==null?"0":"1",manager.getKeyBlockDetails(Arrays.copyOfRange(r,8,r.length))==null?"0":"1",old==null?"-":hex(old.getRequest()),old!=null&&old.isDirect()?"1":"0",Integer.toString(manager.getDirectKeyBlocks().length)));calls++;
 }
 public static void main(String[] args)throws Exception{
  Path container=Paths.get(args[0]);Files.createDirectories(container);root=Files.createTempDirectory(container,"owned-");KeyPairGenerator generator=KeyPairGenerator.getInstance("RSA");SecureRandom random=SecureRandom.getInstance("SHA1PRNG");random.setSeed(new byte[]{1,9,4,7,2});generator.initialize(1536,random);pair=generator.generateKeyPair();Field key=field("key_block_public_key");Object official=key.get(null);RSAPublicKey publicKey=(RSAPublicKey)pair.getPublic();rows.add("test-key\t"+publicKey.getModulus().toString(16)+"\t"+publicKey.getPublicExponent().toString(16));
  try{
   key.set(null,publicKey);
   for(boolean direct:new boolean[]{false,true}){String name=direct?"direct":"indirect";DHTPluginStorageManager m=manager(name);
    operation(name,m,direct,1,100,true);operation(name,m,direct,1,101,true);operation(name,m,direct,1,102,false);operation(name,m,!direct,1,103,true);operation(name,m,true,0,99,true);operation(name,m,true,0,104,true);operation(name,m,false,1,105,true);operation(name,m,true,1,103,true);operation(name,m,true,1,106,true);
   }
   for(int flag:new int[]{0,1,2,255})for(boolean direct:new boolean[]{false,true})for(boolean valid:new boolean[]{false,true}){String name="fresh-"+flag+"-"+direct+"-"+valid;operation(name,manager(name),direct,flag,Integer.MIN_VALUE,valid);}
   for(int[] times:new int[][]{{Integer.MAX_VALUE,Integer.MIN_VALUE},{Integer.MIN_VALUE,Integer.MAX_VALUE},{7,7}}){String name="signed-time-"+times[0];DHTPluginStorageManager m=manager(name);operation(name,m,true,1,times[0],true);operation(name,m,true,0,times[1],true);}
   DHTPluginStorageManager m=manager("filter");byte[] r=request(1,123),valid=sign(r),bad=valid.clone();bad[0]^=1;
   if(DHTPluginStorageManager.verifyKeyBlock(r,bad)||!DHTPluginStorageManager.verifyKeyBlock(r,valid))throw new AssertionError("Actual static RSA verifier");
   if(m.keyBlockRequest(contact(1),r,bad)!=null||m.keyBlockRequest(contact(1),r,valid)!=null)throw new AssertionError("Actual failed verification throttle");
   if(m.keyBlockRequest(contact(2),r,valid)==null)throw new AssertionError("Unrelated origin blocked");
   m=manager("filter-reset");if(m.keyBlockRequest(contact(1),r,bad)!=null)throw new AssertionError();field("kb_verify_fail_bloom_create_time").setLong(m,SystemTime.getCurrentTime()-1800001);if(m.keyBlockRequest(contact(1),r,valid)==null)throw new AssertionError("Actual strict expiry reset");
   rows.add("signature\t"+hex(r)+"\t"+hex(valid)+"\t1");rows.add("signature\t"+hex(r)+"\t"+hex(bad)+"\t0");r[27]^=1;rows.add("signature\t"+hex(r)+"\t"+hex(valid)+"\t0");
   Files.write(Paths.get(args[1]),rows);System.out.println("ORIGINAL_KEY_BLOCK_POLICY operations="+calls+" signature_cases=3 actual_failure_filter_groups=2 PASS");
  }finally{key.set(null,official);try(java.util.stream.Stream<Path> paths=Files.walk(root)){for(Path p:(Iterable<Path>)paths.sorted(Comparator.reverseOrder())::iterator)Files.delete(p);}}
  com.biglybt.core.security.SESecurityManager.exitVM(0);
 }
}
