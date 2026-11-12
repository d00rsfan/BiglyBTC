// SPDX-License-Identifier: GPL-2.0-or-later
// Test-only original Java UDP transport, generated loopback profile and values.
import com.biglybt.core.config.COConfigurationManager;
import com.biglybt.core.dht.DHTLogger;
import com.biglybt.core.dht.transport.*;
import com.biglybt.core.dht.transport.udp.impl.DHTTransportUDPImpl;
import java.io.*;
import java.lang.reflect.*;
import java.security.Permission;
import java.util.*;
import java.util.concurrent.ConcurrentHashMap;
public final class AzureusTorrentPeer {
 static Object zero(Class<?> t){if(!t.isPrimitive()||t==void.class)return null;if(t==boolean.class)return false;if(t==long.class)return 0L;if(t==byte.class)return (byte)0;if(t==short.class)return (short)0;if(t==float.class)return 0F;if(t==double.class)return 0D;return 0;}
 @SuppressWarnings("unchecked") static <T>T proxy(Class<T> t,InvocationHandler h){return(T)Proxy.newProxyInstance(t.getClassLoader(),new Class<?>[]{t},h);}
 static final Map<String,Map<String,DHTTransportValue>> values=new ConcurrentHashMap<>();
 static byte[] none(int length){byte[] out=new byte[length];Arrays.fill(out,(byte)1);return out;}
 static String key(byte[] b){return HexFormat.of().formatHex(b);}
 static final class LoopbackOnly extends SecurityManager {
  public void checkPermission(Permission p){}
  public void checkConnect(String host,int port){try{if(!host.equals("127.0.0.1")&&!host.equals("::1")&&!host.equals("0:0:0:0:0:0:0:1"))throw new SecurityException("Loopback test only");}catch(RuntimeException e){throw e;}}
  public void checkAccept(String host,int port){checkConnect(host,port);}
 }
 public static void main(String[] args)throws Exception {
  if(args.length<3||args.length>4)throw new IOException("generatedProfile network port [rcm]");
  final boolean rcm=args.length==4&&args[3].equals("rcm");
  File root=new File(args[0]);if(!root.isDirectory())throw new IOException("Generated profile required");
  System.setProperty("azureus.config.path",root.getAbsolutePath());System.setProperty("user.home",root.getAbsolutePath());
  COConfigurationManager.initialiseFromMap(new HashMap<String,Object>());COConfigurationManager.setParameter("Ip Filter Enabled",false);
  System.setSecurityManager(new LoopbackOnly());
  final int network=Integer.parseInt(args[1]),port=Integer.parseInt(args[2]);final boolean v6=network==3;final String ip=v6?"::1":"127.0.0.1";
  DHTLogger logger=proxy(DHTLogger.class,(p,m,a)->zero(m.getReturnType()));
  DHTTransportUDPImpl transport=new DHTTransportUDPImpl((byte)55,network,v6,ip,ip,port,3,1,5000,0,0,false,true,logger);
  transport.setRequestHandler(proxy(DHTTransportRequestHandler.class,(p,m,a)->{
   switch(m.getName()){
    case "findNodeRequest":return new DHTTransportContact[0];
    case "getTransportEstimatedDHTSize":return 1;
    case "storeRequest": {
     byte[][] keys=(byte[][])a[1];DHTTransportValue[][] sets=(DHTTransportValue[][])a[2];
     for(int n=0;n<keys.length;n++){if(keys[n].length!=20)throw new IOException("Wire key must be SHA1(original infohash)");final Map<String,DHTTransportValue> entries=values.computeIfAbsent(key(keys[n]),unused->new ConcurrentHashMap<>());
      for(DHTTransportValue value:sets[n]){String owner=key(value.getOriginator().getID());String payload=new String(value.getValue(),java.nio.charset.StandardCharsets.US_ASCII);if(payload.contains("http")||payload.contains("generated-secret"))throw new IOException("Credential in DHT value");if(value.getValue().length==0)entries.remove(owner);else if((rcm&&value.getFlags()==0)||(!rcm&&(value.getFlags()==1||value.getFlags()==2))){if(rcm){Map decoded=com.biglybt.core.util.BDecoder.decode(value.getValue());if(!(decoded.get("h") instanceof byte[])||((byte[])decoded.get("h")).length!=20||!(decoded.get("d") instanceof byte[]))throw new IOException("Original RCM wire fields");}entries.put(owner,value);}else throw new IOException("Original tracker flag");}}
     return proxy(DHTTransportStoreReply.class,(x,q,b)->q.getName().equals("getDiversificationTypes")?none(keys.length):zero(q.getReturnType()));
    }
    case "findValueRequest": {
     final Map<String,DHTTransportValue> entries=values.get(key((byte[])a[1]));final DHTTransportValue[] found=entries==null?new DHTTransportValue[0]:entries.values().toArray(new DHTTransportValue[0]);
     return proxy(DHTTransportFindValueReply.class,(x,q,b)->{switch(q.getName()){case "hit":return found.length>0;case "getValues":return found;case "getContacts":return new DHTTransportContact[0];case "getDiversificationType":return (byte)1;default:return zero(q.getReturnType());}});
    }
    default:return zero(m.getReturnType());
   }
  }));
  System.out.println("READY "+port);System.out.flush();
  try(BufferedReader in=new BufferedReader(new InputStreamReader(System.in,java.nio.charset.StandardCharsets.US_ASCII))){for(String line;(line=in.readLine())!=null;){if(line.equals("EXIT"))break;String[] bits=line.split(" ");if(bits.length!=3||(!bits[0].equals("HAS")&&!(rcm&&bits[0].equals("ADD"))))throw new IOException("Owned inventory command");if(bits[0].equals("ADD")){Map<String,Object> record=new HashMap<>();record.put("d","Generated Java finding".getBytes(java.nio.charset.StandardCharsets.UTF_8));record.put("h",HexFormat.of().parseHex(bits[2]));record.put("r",37L);record.put("s",17001L);record.put("z",4L);record.put("l",2L);byte[] payload=com.biglybt.core.util.BEncoder.encode(record);DHTTransportValue value=proxy(DHTTransportValue.class,(p,m,a)->{switch(m.getName()){case "getValue":return payload;case "getFlags":return 0;case "getCreationTime":return System.currentTimeMillis();case "getOriginator":return transport.getLocalContact();case "getVersion":return 1;case "isLocal":return true;default:return zero(m.getReturnType());}});values.computeIfAbsent(bits[1],unused->new ConcurrentHashMap<>()).put("java-generated",value);System.out.println("ADDED");System.out.flush();continue;}Map<String,DHTTransportValue> entries=values.get(bits[1]);boolean has=entries!=null&&entries.values().stream().anyMatch(v->{String s=new String(v.getValue(),java.nio.charset.StandardCharsets.US_ASCII);if(rcm){try{Map decoded=com.biglybt.core.util.BDecoder.decode(v.getValue());return key((byte[])decoded.get("h")).equals(bits[2]);}catch(Exception e){throw new IllegalStateException(e);}}return s.equals(bits[2])||s.startsWith(bits[2]+";");});System.out.println("HAS "+(has?1:0));System.out.flush();}}
  System.exit(0);
 }
}
