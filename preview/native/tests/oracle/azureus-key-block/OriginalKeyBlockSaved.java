// SPDX-License-Identifier: GPL-2.0-or-later
package com.biglybt.plugin.dht.impl;
import java.lang.reflect.*;
import java.nio.file.*;
import java.security.*;
import java.security.interfaces.RSAPublicKey;
import java.util.*;
import com.biglybt.core.util.*;
import sun.misc.Unsafe;

/** Actual original readKeyBlocks on generated files and an isolated clock/key. */
public final class OriginalKeyBlockSaved {
 static long now=2000000000000L;
 public static void main(String[] args)throws Exception{
  SystemTime.getCurrentTime();Field uf=Unsafe.class.getDeclaredField("theUnsafe");uf.setAccessible(true);Unsafe u=(Unsafe)uf.get(null);Field time=SystemTime.class.getDeclaredField("instance");time.setAccessible(true);Object originalTime=time.get(null);Class<?> provider=Class.forName("com.biglybt.core.util.SystemTime$SystemTimeProvider");Object clock=Proxy.newProxyInstance(provider.getClassLoader(),new Class[]{provider},(p,m,a)->now);
  Field key=OriginalKeyBlockPolicy.field("key_block_public_key");Object official=key.get(null);KeyPairGenerator generator=KeyPairGenerator.getInstance("RSA");SecureRandom random=SecureRandom.getInstance("SHA1PRNG");random.setSeed(new byte[]{1,9,4,7,2});generator.initialize(1536,random);OriginalKeyBlockPolicy.pair=generator.generateKeyPair();RSAPublicKey pub=(RSAPublicKey)OriginalKeyBlockPolicy.pair.getPublic();List<String> rows=new ArrayList<>();rows.add("test-key\t"+pub.getModulus().toString(16)+"\t"+pub.getPublicExponent().toString(16));Path container=Paths.get(args[0]);Files.createDirectories(container);OriginalKeyBlockPolicy.root=Files.createTempDirectory(container,"owned-");
  try{
   key.set(null,pub);u.putObjectVolatile(u.staticFieldBase(time),u.staticFieldOffset(time),clock);int n=0;
   for(long direct:new long[]{0,1,2})for(int flag:new int[]{0,1})for(long age:new long[]{0,604799,604800,604801,-10,4294967296L}){
    final String name="saved-"+(n++);Path dir=OriginalKeyBlockPolicy.root.resolve(name);Files.createDirectories(dir);byte[] request=OriginalKeyBlockPolicy.request(flag,123),signature=OriginalKeyBlockPolicy.sign(request);Map<String,Object> entry=new HashMap<>();entry.put("req",request);entry.put("cert",signature);entry.put("received",now/1000-age);entry.put("direct",direct);entry.put("opaque-entry","retained".getBytes("UTF-8"));Map<String,Object> root=new HashMap<>();root.put("entries",Arrays.asList(entry));root.put("opaque-root","retained".getBytes("UTF-8"));byte[] encoded=BEncoder.encode(root);Files.write(dir.resolve("block.dat"),encoded);DHTPluginStorageManager manager=OriginalKeyBlockPolicy.manager(name);DHTPluginStorageManager.keyBlock loaded=OriginalKeyBlockPolicy.stored(manager);
    rows.add(String.join("\t",name,Long.toString(now),OriginalKeyBlockPolicy.hex(encoded),loaded==null?"0":"1",loaded==null?"0":Integer.toString(loaded.getReceived()),loaded!=null&&loaded.isDirect()?"1":"0",Integer.toString(manager.getDirectKeyBlocks().length)));
   }
   Files.write(Paths.get(args[1]),rows);System.out.println("ORIGINAL_KEY_BLOCK_SAVED actual_load_cases="+n+" fixed_wall_clock=1 PASS");
  }finally{
   key.set(null,official);u.putObjectVolatile(u.staticFieldBase(time),u.staticFieldOffset(time),originalTime);try(java.util.stream.Stream<Path> paths=Files.walk(OriginalKeyBlockPolicy.root)){for(Path p:(Iterable<Path>)paths.sorted(Comparator.reverseOrder())::iterator)Files.delete(p);}
  }
  com.biglybt.core.security.SESecurityManager.exitVM(0);
 }
}
