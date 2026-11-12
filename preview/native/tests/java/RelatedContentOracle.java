// SPDX-License-Identifier: GPL-2.0-or-later
// Generated-only interoperability oracle using the pinned original APIs.
import com.biglybt.core.config.COConfigurationManager;
import com.biglybt.core.content.RelatedContent;
import com.biglybt.core.content.RelatedContentManager;
import com.biglybt.core.security.CryptoManagerFactory;
import com.biglybt.core.util.*;
import com.biglybt.util.MapUtils;
import java.io.*;
import java.lang.reflect.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.security.MessageDigest;
import java.util.*;
import java.util.zip.*;
public final class RelatedContentOracle {
 static byte[] obs(){byte[] out=new byte[20];for(int i=0;i<out.length;i++)out[i]=(byte)(i*9+3);return out;}
 static byte[] bytes(int start){byte[] out=new byte[20];for(int i=0;i<out.length;i++)out[i]=(byte)(start+i);return out;}
 static Method method(String name)throws Exception {for(Method m:RelatedContentManager.class.getDeclaredMethods()){if(m.getName().equals(name)){m.setAccessible(true);return m;}}throw new Exception(name);}
 @SuppressWarnings("unchecked") static Map<String,Object> map(byte[] data)throws Exception {return(Map<String,Object>)BDecoder.decode(data);}
 static byte[] pack(Map<String,Object> inner)throws Exception {ByteArrayOutputStream out=new ByteArrayOutputStream();try(GZIPOutputStream gz=new GZIPOutputStream(out)){gz.write(BEncoder.encode(inner));}return CryptoManagerFactory.getSingleton().obfuscate(out.toByteArray());}
 static Map<String,Object> unpack(Map<String,Object> outer)throws Exception {byte[] data=CryptoManagerFactory.getSingleton().deobfuscate((byte[])outer.get("d"));try(GZIPInputStream gz=new GZIPInputStream(new ByteArrayInputStream(data))){return map(gz.readAllBytes());}}
 static void check(boolean value,String why)throws Exception {if(!value)throw new Exception(why);}
 public static void main(String[] args)throws Exception {
  check(args.length==2,"emit|verify generatedRoot");Path root=Path.of(args[1]).toAbsolutePath();check(Files.isDirectory(root),"Generated root exists");System.setProperty("azureus.config.path",root.toString());System.setProperty("user.home",root.toString());Map<String,Object> config=new HashMap<>();config.put("core.crypto.obs.id",obs());COConfigurationManager.initialiseFromMap(config);
  Field field=sun.misc.Unsafe.class.getDeclaredField("theUnsafe");field.setAccessible(true);sun.misc.Unsafe unsafe=(sun.misc.Unsafe)field.get(null);RelatedContentManager manager=(RelatedContentManager)unsafe.allocateInstance(RelatedContentManager.class);
  if(args[0].equals("emit")){
   Map<String,Object> wire=new HashMap<>();wire.put("d","Згенерована знахідка 🌍".getBytes(StandardCharsets.UTF_8));wire.put("h",bytes(1));wire.put("r",37L);wire.put("s",1048579L);wire.put("v",1L);wire.put("p",400000L);wire.put("z",17L);wire.put("l",3L);wire.put("g",method("encodeTags").invoke(manager,(Object)new String[]{"Generated","Кирилиця","🌍🌍🌍🌍🌍🌍"}));wire.put("k",new byte[]{1,2,3,4});
   RelatedContent item=(RelatedContent)method("decodeInfo").invoke(manager,wire,bytes(42),1,true,new HashSet<String>());check(item!=null&&item.getSeeds()==17&&item.getLeechers()==3,"Original wire decode");
   @SuppressWarnings("unchecked") Map<String,Object> row=(Map<String,Object>)method("serialiseDI").invoke(manager,item,null);row.put("_i",17L);row.put("_k",Base32.encode(bytes(1)));row.put("u",1L);row.put("f",1000L);row.put("s",2000L);row.put("e",1L);row.put("cl",300000L);row.put("future-row",Map.of("keep",73L));MapUtils.exportIntArrayAsByteArray(row,"l",new int[]{11,12});
   Map<String,Object> links=new HashMap<>();MapUtils.exportIntArrayAsByteArray(links,Base32.encode(bytes(42)),new int[]{17});Map<String,Object> inner=new HashMap<>();inner.put("rc",List.of(row));inner.put("rcm",links);inner.put("future-root",Map.of("keep",91L));Map<String,Object> outer=new HashMap<>();outer.put("d",pack(inner));outer.put("future-envelope",Map.of("keep",39L));
   Files.write(root.resolve("java-rcm.config"),BEncoder.encode(outer));Files.write(root.resolve("java-wire.ben"),BEncoder.encode(wire));Files.write(root.resolve("obs.key"),obs());Map<String,Object> del=new HashMap<>();del.put("entries",List.of(Arrays.copyOf(bytes(99),8)));del.put("future-delete",42L);Files.write(root.resolve("java-rcmx.config"),BEncoder.encode(del));
   row=new HashMap<>(row);row.remove("_k");inner=new HashMap<>(inner);inner.put("rc",Map.of(Base32.encode(bytes(1)),row));Files.write(root.resolve("java-legacy.config"),BEncoder.encode(inner));
   Files.write(root.resolve("java-association.key"),MessageDigest.getInstance("SHA-1").digest(("az:rcm:assoc:"+ByteFormatter.encodeString(bytes(42))).getBytes(StandardCharsets.UTF_8)));
   System.out.println("PASS_ORIGINAL_RCM_FIXTURES");
  }else if(args[0].equals("verify")){
   Map<String,Object> outer=map(Files.readAllBytes(root.resolve("native-rcm.config"))),inner=unpack(outer);check(((Map<?,?>)outer.get("future-envelope")).get("keep").equals(39L),"Envelope unknown field");check(((Map<?,?>)inner.get("future-root")).get("keep").equals(91L),"Inner unknown field");List<?> rows=(List<?>)inner.get("rc");check(rows.size()==1,"Original reader sees current records");@SuppressWarnings("unchecked") Map<String,Object> row=(Map<String,Object>)rows.get(0);check(((Map<?,?>)row.get("future-row")).get("keep").equals(73L),"Row unknown field");check(row.get("u").equals(0L),"Native unread flag readable");RelatedContent item=(RelatedContent)method("deserialiseDI").invoke(manager,row,null);check(item!=null&&item.getSeeds()==17&&item.getTitle().equals("Згенерована знахідка 🌍"),"Original native cache decode");
   @SuppressWarnings("unchecked") Map<String,Object> relationships=(Map<String,Object>)inner.get("rcm");int[] ids=MapUtils.importIntArray(relationships,Base32.encode(bytes(42)));check(ids.length==1&&ids[0]==((Long)row.get("_i")).intValue(),"Original reindexed relationship");Map<String,Object> del=map(Files.readAllBytes(root.resolve("native-rcmx.config")));check(del.get("future-delete").equals(42L),"Deletion unknown field");
   Map<String,Object> wire=map(Files.readAllBytes(root.resolve("native-wire.ben")));RelatedContent found=(RelatedContent)method("decodeInfo").invoke(manager,wire,bytes(42),1,false,new HashSet<String>());check(found!=null&&found.getSeeds()==17&&found.getLeechers()==3,"Original native wire decode");System.out.println("PASS_ORIGINAL_RCM_ROUNDTRIP");
  }else throw new Exception("Unknown oracle mode");
 }
}
