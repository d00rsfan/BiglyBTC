package com.vuze.plugins.azmsgsync;
import java.io.*;import java.net.*;import java.lang.reflect.*;import java.security.Security;import java.util.*;
import com.biglybt.plugin.dht.*;import com.biglybt.core.util.*;import org.gudy.bouncycastle.jce.provider.BouncyCastleProvider;
/** Unmodified original room handler. Host/DHT are generated in-memory test adapters. */
public final class FullHandlerOracle {
 static final DataInputStream input=new DataInputStream(System.in);static final DataOutputStream output=new DataOutputStream(System.out);
 static boolean syncing=false;
 static void frame(int type,byte[] data)throws Exception{output.writeInt(type);output.writeInt(data.length);output.write(data);output.flush();}
 static byte[] bytes()throws Exception{int length=input.readInt();if(length<0||length>1048576)throw new IOException("Frame bound");byte[] data=new byte[length];input.readFully(data);return data;}
 static Object zero(Class<?> type){if(type==boolean.class)return false;if(type==byte.class)return (byte)0;if(type==int.class)return 0;if(type==long.class)return 0L;return null;}
 static DHTPluginContact contact(final int last,final int port){return (DHTPluginContact)java.lang.reflect.Proxy.newProxyInstance(FullHandlerOracle.class.getClassLoader(),new Class[]{DHTPluginContact.class},(proxy,method,args)->{
  String name=method.getName();if(name.equals("getAddress"))return new InetSocketAddress(InetAddress.getByAddress(new byte[]{127,123,31,(byte)last}),port);if(name.equals("getProtocolVersion"))return (byte)55;if(name.equals("getNetwork"))return 0;if(name.equals("getID"))return new byte[20];if(name.equals("getName"))return "Generated contact";
  if(name.equals("exportToMap")){Map<String,Object> map=new HashMap<>();map.put("a",new byte[]{127,123,31,(byte)last});map.put("p",(long)port);map.put("v",55L);return map;}
  if(name.equals("hashCode"))return last;if(name.equals("equals"))return proxy==args[0];if(name.equals("toString"))return "Generated contact";
  if(name.equals("call")){if(!syncing)throw new IOException("Unrequested test call");frame(3,(byte[])args[2]);if(input.readInt()!=4)throw new IOException("Expected Native room response");return bytes();}
  return zero(method.getReturnType());
 });}
 public static void main(String[] args)throws Exception{
  System.setOut(new PrintStream(System.err,true,"UTF-8"));com.biglybt.core.config.COConfigurationManager.initialiseFromMap(new HashMap<String,Object>());Security.addProvider(new BouncyCastleProvider());final DHTPluginContact local=contact(1,45321),remote=contact(2,45322);
  DHTPluginInterface dht=(DHTPluginInterface)java.lang.reflect.Proxy.newProxyInstance(FullHandlerOracle.class.getClassLoader(),new Class[]{DHTPluginInterface.class},(proxy,method,values)->{
   String name=method.getName();if(name.equals("getNetwork"))return AENetworkClassifier.AT_PUBLIC;if(name.equals("getLocalAddress"))return local;if(name.equals("getLocalAddresses"))return new DHTPluginContact[]{local};if(name.equals("importContact"))return remote;if(name.equals("isEnabled"))return true;if(name.equals("hashCode"))return 1;if(name.equals("equals"))return proxy==values[0];return zero(method.getReturnType());
  });
  final MsgSyncHandler handler=new MsgSyncHandler(new MsgSyncPlugin(),dht,"Generated full Java room".getBytes("UTF-8"),new HashMap<String,Object>());
  Map<String,Object> message=new HashMap<>();message.put("msg","Generated Java message: Привіт 🌍".getBytes("UTF-8"));message.put("nick","Generated Java".getBytes("UTF-8"));handler.sendMessage(BEncoder.encode(message),null,new HashMap<String,Object>());
  if(handler.getMessages().size()!=1)throw new IOException("Original local message missing");frame(0,new byte[0]);
  while(true){int type=input.readInt();byte[] payload=bytes();
   if(type==1){byte[] reply=handler.handleRead(remote,payload);if(reply==null)throw new IOException("Original handler refused Native request");frame(2,reply);}
   else if(type==5){byte[] uid=new byte[8];uid[0]=99;Method get=MsgSyncHandler.class.getDeclaredMethod("getNodes",byte[].class);get.setAccessible(true);List<?> nodes=(List<?>)get.invoke(handler,(Object)uid);if(nodes==null||nodes.isEmpty())throw new IOException("Original handler did not retain the Native requester");Method sync=MsgSyncHandler.class.getDeclaredMethod("sync",MsgSyncNode.class,boolean.class);sync.setAccessible(true);syncing=true;try{sync.invoke(handler,nodes.get(0),true);}finally{syncing=false;}
    boolean found=false;for(MsgSyncMessage m:handler.getMessages()){Map decoded=BDecoder.decode(m.getContent());Object text=decoded.get("msg");if(text instanceof byte[]&&new String((byte[])text,"UTF-8").equals("Generated Native reply: Дякую 🌍"))found=true;}if(!found)throw new IOException("Original handler did not admit Native signed reply");frame(6,"PASS full original handler in both directions".getBytes("UTF-8"));
   }else if(type==7){if(payload.length!=21)throw new IOException("Key fixture bound");frame(8,com.biglybt.plugin.dht.impl.DHTPluginStorageManager.diversifyKey(Arrays.copyOf(payload,20),payload[20]&255));}
   else if(type==9){handler.destroy(true);System.exit(0);}else throw new IOException("Unknown test command");
  }
 }
}
