// SPDX-License-Identifier: GPL-2.0-or-later
package com.biglybt.core.dht.transport.udp.impl;
import java.io.*;
import java.lang.reflect.*;
import java.net.*;
import java.nio.file.*;
import java.util.*;
import com.biglybt.net.udp.uc.*;
import com.biglybt.core.dht.transport.udp.impl.packethandler.DHTUDPPacketNetworkHandler;
import sun.misc.Unsafe;

/** Actual original packet serializers and reply parser; generated fields only. */
public final class OriginalKeyBlock {
 static final Unsafe U;
 static {try{Field f=Unsafe.class.getDeclaredField("theUnsafe");f.setAccessible(true);U=(Unsafe)f.get(null);}catch(Exception e){throw new RuntimeException(e);}}
 static void set(Object o,Class<?> c,String name,Object v)throws Exception{Field f=c.getDeclaredField(name);f.setAccessible(true);f.set(o,v);}
 static void base(Object o,int action)throws Exception{set(o,PRUDPPacket.class,"type",action);set(o,PRUDPPacket.class,"transaction_id",17);}
 static byte[] wire(PRUDPPacket p)throws Exception{ByteArrayOutputStream b=new ByteArrayOutputStream();p.serialise(new DataOutputStream(b));return b.toByteArray();}
 static String hex(byte[] b){StringBuilder s=new StringBuilder();for(byte x:b)s.append(String.format("%02x",x&255));return s.toString();}
 static byte[] bytes(int n,int start){byte[] b=new byte[n];for(int i=0;i<n;i++)b[i]=(byte)(i+start);return b;}

 static DHTUDPPacketRequestKeyBlock request(int version,int requestLength,int signatureLength)throws Exception{
  DHTUDPPacketRequestKeyBlock p=(DHTUDPPacketRequestKeyBlock)U.allocateInstance(DHTUDPPacketRequestKeyBlock.class);
  DHTTransportUDPImpl t=(DHTTransportUDPImpl)U.allocateInstance(DHTTransportUDPImpl.class);set(t,DHTTransportUDPImpl.class,"protocol_version",(byte)55);
  base(p,1036);set(p,PRUDPPacketRequest.class,"connection_id",Long.MIN_VALUE+77);set(p,DHTUDPPacketRequest.class,"transport",t);set(p,DHTUDPPacketRequest.class,"protocol_version",(byte)version);set(p,DHTUDPPacketRequest.class,"originator_address",new InetSocketAddress("127.0.0.1",4444));set(p,DHTUDPPacketRequest.class,"originator_instance_id",29);set(p,DHTUDPPacketRequest.class,"originator_time",1000L);set(p,DHTUDPPacketRequest.class,"flags",(byte)1);set(p,DHTUDPPacketRequest.class,"flags2",(byte)2);
  p.setRandomID(Integer.MIN_VALUE+77);p.setKeyBlockDetails(bytes(requestLength,1),bytes(signatureLength,128));return p;
 }
 static DHTUDPPacketReplyKeyBlock reply(int version)throws Exception{
  DHTUDPPacketReplyKeyBlock p=(DHTUDPPacketReplyKeyBlock)U.allocateInstance(DHTUDPPacketReplyKeyBlock.class);base(p,1037);set(p,DHTUDPPacketReply.class,"connection_id",Long.MIN_VALUE+77);set(p,DHTUDPPacketReply.class,"protocol_version",(byte)version);set(p,DHTUDPPacketReply.class,"target_instance_id",29);set(p,DHTUDPPacketReply.class,"flags",(byte)1);set(p,DHTUDPPacketReply.class,"flags2",(byte)2);return p;
 }
 static void verifyReply(byte[] b)throws Exception{
  DHTUDPPacketNetworkHandler h=new DHTUDPPacketNetworkHandler(null,0){@Override public DHTTransportUDPImpl getTransport(DHTUDPPacket p){return null;}};
  DataInputStream in=new DataInputStream(new ByteArrayInputStream(b));if(in.readInt()!=1037)throw new AssertionError();int transaction=in.readInt();DHTUDPPacketReplyKeyBlock p=new DHTUDPPacketReplyKeyBlock(h,new InetSocketAddress("127.0.0.1",4444),in,transaction);if(in.available()!=0||!Arrays.equals(wire(p),b))throw new AssertionError("Original ACK reserialise");
 }
 public static void main(String[] args)throws Exception{
  List<String> lines=new ArrayList<>();for(int version:new int[]{51,55}){
   for(int[] sizes:new int[][]{{0,0},{9,0},{28,128},{28,256},{255,256},{28,32768},{28,65535}}){byte[] b=wire(request(version,sizes[0],sizes[1]));lines.add("request-"+version+"-"+sizes[0]+"-"+sizes[1]+"\trequest\t"+hex(b));}
   byte[] ack=wire(reply(version));verifyReply(ack);lines.add("reply-"+version+"\treply\t"+hex(ack));
  }
  int rejected=0;for(int[] sizes:new int[][]{{256,128},{28,65536}}){try{wire(request(55,sizes[0],sizes[1]));}catch(IOException e){rejected++;}}
  if(rejected!=2)throw new AssertionError("Original declared wire limits");
  if(com.biglybt.plugin.dht.impl.DHTPluginStorageManager.verifyKeyBlock(bytes(28,1),bytes(128,2)))throw new AssertionError("Unsigned request accepted");
  Files.write(Paths.get(args[0]),lines);System.out.println("ORIGINAL_KEY_BLOCK wire_cases="+lines.size()+" reply_decode=2 oversize_refusals=2 unsigned_refused=1 PASS");com.biglybt.core.security.SESecurityManager.exitVM(0);
 }
}
