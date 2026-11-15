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
public final class OriginalQueryStorage {
 static final Unsafe U;
 static {try{Field f=Unsafe.class.getDeclaredField("theUnsafe");f.setAccessible(true);U=(Unsafe)f.get(null);}catch(Exception e){throw new RuntimeException(e);}}
 static void set(Object o,Class<?> c,String name,Object v)throws Exception{Field f=c.getDeclaredField(name);f.setAccessible(true);f.set(o,v);}
 static void base(Object o,int action)throws Exception{set(o,PRUDPPacket.class,"type",action);set(o,PRUDPPacket.class,"transaction_id",17);}
 static byte[] wire(PRUDPPacket p)throws Exception{ByteArrayOutputStream b=new ByteArrayOutputStream();p.serialise(new DataOutputStream(b));return b.toByteArray();}
 static String hex(byte[] b){StringBuilder s=new StringBuilder();for(byte x:b)s.append(String.format("%02x",x&255));return s.toString();}
 static byte[] bytes(int n,int start){byte[] b=new byte[n];for(int i=0;i<n;i++)b[i]=(byte)(i+start);return b;}
 static DHTUDPPacketRequestQueryStorage request(int version,int mode)throws Exception{
  DHTUDPPacketRequestQueryStorage p=(DHTUDPPacketRequestQueryStorage)U.allocateInstance(DHTUDPPacketRequestQueryStorage.class);
  DHTTransportUDPImpl t=(DHTTransportUDPImpl)U.allocateInstance(DHTTransportUDPImpl.class);set(t,DHTTransportUDPImpl.class,"protocol_version",(byte)55);
  base(p,1038);set(p,PRUDPPacketRequest.class,"connection_id",Long.MIN_VALUE+77);set(p,DHTUDPPacketRequest.class,"transport",t);set(p,DHTUDPPacketRequest.class,"protocol_version",(byte)version);set(p,DHTUDPPacketRequest.class,"originator_address",new InetSocketAddress("127.0.0.1",4444));set(p,DHTUDPPacketRequest.class,"originator_instance_id",29);set(p,DHTUDPPacketRequest.class,"originator_time",1000L);set(p,DHTUDPPacketRequest.class,"flags",(byte)1);set(p,DHTUDPPacketRequest.class,"flags2",(byte)2);
  List<Object[]> groups=new ArrayList<>();if(mode>0){int prefix=mode==1?0:mode==2?3:6;List<byte[]> suffixes=new ArrayList<>();suffixes.add(bytes(6-prefix,10));suffixes.add(bytes(6-prefix,30));groups.add(new Object[]{bytes(prefix,1),suffixes});if(mode==4)groups.add(new Object[]{bytes(2,99),Arrays.asList(bytes(4,55))});}p.setDetails(6,groups);return p;
 }
 static DHTUDPPacketReplyQueryStorage reply(int version,int n,boolean all)throws Exception{
  DHTUDPPacketReplyQueryStorage p=(DHTUDPPacketReplyQueryStorage)U.allocateInstance(DHTUDPPacketReplyQueryStorage.class);base(p,1039);set(p,DHTUDPPacketReply.class,"connection_id",Long.MIN_VALUE+77);set(p,DHTUDPPacketReply.class,"protocol_version",(byte)version);set(p,DHTUDPPacketReply.class,"target_instance_id",29);set(p,DHTUDPPacketReply.class,"flags",(byte)1);set(p,DHTUDPPacketReply.class,"flags2",(byte)2);
  List<byte[]> rows=new ArrayList<>();for(int i=0;i<n;i++)rows.add(all||i%3==1?bytes(2,i):null);p.setResponse(2,rows);return p;
 }
 static void verifyReply(byte[] b,DHTUDPPacketReplyQueryStorage source)throws Exception{
  DHTUDPPacketNetworkHandler h=new DHTUDPPacketNetworkHandler(null,0){@Override public DHTTransportUDPImpl getTransport(DHTUDPPacket p){return null;}};
  DataInputStream in=new DataInputStream(new ByteArrayInputStream(b));if(in.readInt()!=1039)throw new AssertionError();int transaction=in.readInt();DHTUDPPacketReplyQueryStorage p=new DHTUDPPacketReplyQueryStorage(h,new InetSocketAddress("127.0.0.1",4444),in,transaction);if(in.available()!=0||p.getResponse().size()!=source.getResponse().size())throw new AssertionError("Original decode size");for(int i=0;i<p.getResponse().size();i++)if(!Arrays.equals(p.getResponse().get(i),source.getResponse().get(i)))throw new AssertionError("Original decode entry");if(!Arrays.equals(wire(p),b))throw new AssertionError("Original reserialise");
 }
 public static void main(String[] args)throws Exception{
  List<String> lines=new ArrayList<>();for(int version:new int[]{51,55}){for(int mode=0;mode<5;mode++){lines.add("request-"+version+"-"+mode+"\trequest\t"+hex(wire(request(version,mode))));}for(int n:new int[]{0,1,7,8,9,17})for(boolean all:new boolean[]{false,true}){DHTUDPPacketReplyQueryStorage p=reply(version,n,all);byte[] b=wire(p);verifyReply(b,p);lines.add("reply-"+version+"-"+n+"-"+all+"\treply\t"+hex(b));}}
  Files.write(Paths.get(args[0]),lines);System.out.println("ORIGINAL_QUERY_STORAGE cases="+lines.size()+" reply_decode=24");com.biglybt.core.security.SESecurityManager.exitVM(0);
 }
}
