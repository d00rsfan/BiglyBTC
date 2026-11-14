// SOURCE ONLY. Actual original DHTTransferHandler class, with a finite owned UDP
// Adapter and the unchanged DATA body serializer/reader projection beside it.
// This is transport evidence; the deterministic handler payload is not chat.
// Raw mode deliberately returns null: original writeTransfer completes on ACK.
package com.biglybt.core.dht.transport.udp.impl;
import com.biglybt.core.dht.DHTLogger;
import com.biglybt.core.dht.transport.*;
import com.biglybt.core.dht.transport.util.DHTTransferHandler;
import java.io.*;
import java.lang.reflect.Proxy;
import java.net.*;
import java.security.SecureRandom;
import java.security.Permission;
import java.util.*;
import java.util.concurrent.atomic.*;
public final class OriginalRawWritePeer {
  static final byte[] HANDLER=new byte[20];static{Arrays.fill(HANDLER,(byte)'H');}
  static final SecureRandom RANDOM=new SecureRandom();
  static byte[] value(int n,byte fill){byte[] out=new byte[n];Arrays.fill(out,fill);return out;}
  static InetAddress numeric(String text)throws Exception {
    String[] parts=text.split("\\.",-1);if(parts.length!=4)throw new IOException("explicit IPv4 only fixture");byte[] bytes=new byte[4];for(int i=0;i<4;i++){int n=Integer.parseInt(parts[i]);if(n<0||n>255)throw new IOException("address");bytes[i]=(byte)n;}InetAddress out=InetAddress.getByAddress(bytes);if(!out.isLoopbackAddress())throw new IOException("owned loopback only");return out;
  }
  static Object defaultValue(Class<?> type){if(type==boolean.class)return false;if(type==byte.class)return(byte)55;if(type==int.class)return 0;if(type==long.class)return 0L;return null;}
  static DHTTransportContact contact(InetSocketAddress peer){return (DHTTransportContact)Proxy.newProxyInstance(OriginalRawWritePeer.class.getClassLoader(),new Class<?>[]{DHTTransportContact.class},(p,m,a)->{
    if(m.getName().equals("getAddress")||m.getName().equals("getTransportAddress")||m.getName().equals("getExternalAddress"))return peer;
    if(m.getName().equals("getID")||m.getName().equals("getBloomKey"))return new byte[20];if(m.getName().equals("getName")||m.getName().equals("toString"))return "owned-original-data-peer";return defaultValue(m.getReturnType());});}
  static byte[] encode(DHTTransferHandler.Packet packet,InetSocketAddress local,int transaction)throws Exception {
    OriginalRawDataBody body=new OriginalRawDataBody();body.packet_type=packet.getPacketType();body.transfer_key=packet.getTransferKey();body.key=packet.getRequestKey();body.data=packet.getData();body.start_position=packet.getStartPosition();body.length=packet.getLength();body.total_length=packet.getTotalLength();
    ByteArrayOutputStream bytes=new ByteArrayOutputStream();DataOutputStream os=new DataOutputStream(bytes);
    // Pinned original PRUDPPacketRequest +DHTUDPPacketRequest header projection.
    os.writeLong(packet.getConnectionId());os.writeInt(1035);os.writeInt(transaction);os.writeByte(55);os.writeByte(com.biglybt.core.dht.transport.udp.DHTTransportUDP.VENDOR_ID_ME);os.writeInt(0);os.writeByte(55);byte[] address=local.getAddress().getAddress();os.writeByte(address.length);os.write(address);os.writeShort(local.getPort());os.writeInt(1);os.writeLong(System.currentTimeMillis());os.writeByte(0);os.writeByte(0);body.serialise(os);os.flush();if(bytes.size()>1430)throw new IOException("DATA physical bound");return bytes.toByteArray();
  }
  static DHTTransferHandler.Packet decode(byte[] bytes,int size,InetSocketAddress actual)throws Exception {
    DataInputStream is=new DataInputStream(new ByteArrayInputStream(bytes,0,size));long connection=is.readLong();if(is.readInt()!=1035)throw new IOException("action");is.readInt();if(is.readUnsignedByte()!=55)throw new IOException("version");is.readByte();if(is.readInt()!=0||is.readUnsignedByte()!=55)throw new IOException("network/version");int width=is.readUnsignedByte();if(width!=4)throw new IOException("fixture family");byte[] origin=new byte[width];is.readFully(origin);int port=is.readUnsignedShort();if(!InetAddress.getByAddress(origin).equals(actual.getAddress())||port!=actual.getPort())throw new IOException("source tuple");is.readInt();is.readLong();is.readByte();is.readByte();OriginalRawDataBody body=new OriginalRawDataBody(is);if(is.available()!=0||body.transfer_key.length!=20||body.key.length!=0||body.total_length<0||body.total_length>16384||body.data.length>1317)throw new IOException("bounded DATA");return new DHTTransferHandler.Packet(connection,body.packet_type,body.transfer_key,body.key,body.data,body.start_position,body.length,body.total_length);
  }
  static final class OwnedFence extends SecurityManager {
    final String peer;final int port;final List<String> reads=new ArrayList<>();
    OwnedFence(InetSocketAddress target){peer=target.getAddress().getHostAddress();port=target.getPort();for(String part:System.getProperty("java.class.path").split(File.pathSeparator)){reads.add(new File(part).getAbsolutePath());}reads.add(new File(System.getProperty("java.home")).getAbsolutePath());}
    public void checkPermission(Permission permission){}
    public void checkConnect(String host,int requested){if(!host.equals(peer)||(requested!=-1&&requested!=port))throw new SecurityException("owned exact UDP peer only");}
    public void checkAccept(String host,int requested){checkConnect(host,requested);}
    public void checkListen(int requested){throw new SecurityException("no additional listener");}
    public void checkRead(String file){String full=new File(file).getAbsolutePath();for(String root:reads){if(full.equals(root)||full.startsWith(root+File.separator))return;}throw new SecurityException("undeclared reference read");}
    public void checkWrite(String file){throw new SecurityException("no reference writes");}
    public void checkDelete(String file){throw new SecurityException("no reference deletes");}
    public void checkExec(String command){throw new SecurityException("no reference children");}
  }
  public static void main(String[] args)throws Exception {
    if(args.length==1&&args[0].equals("--vectors")){
      for(int width:new int[]{4,16}){byte[] raw=new byte[width];raw[width-1]=1;if(width==4)raw[0]=127;InetSocketAddress local=new InetSocketAddress(InetAddress.getByAddress(raw),12345);DHTTransferHandler.Packet packet=new DHTTransferHandler.Packet(Long.MIN_VALUE,(byte)2,HANDLER,value(20,(byte)'K'),value(4096,(byte)'x'),0,1317,4096);byte[] encoded=encode(packet,local,99);if(encoded.length!=(width==4?1418:1430))throw new IOException("original physical envelope");StringBuilder hex=new StringBuilder();for(byte b:encoded)hex.append(String.format(Locale.ROOT,"%02x",b&255));System.out.println(width+","+encoded.length+","+hex);}
      return;
    }
    if(args.length!=6)throw new IOException("bindIPv4 bindPort peerIPv4 peerPort caller|receiver payloadBytes");
    InetSocketAddress local=new InetSocketAddress(numeric(args[0]),Integer.parseInt(args[1])),peer=new InetSocketAddress(numeric(args[2]),Integer.parseInt(args[3]));if(peer.getPort()==0)throw new IOException("exact peer required");boolean caller=args[4].equals("caller");int payloadBytes=Integer.parseInt(args[5]);if(payloadBytes<1||payloadBytes>16384)throw new IOException("payload bound");if(!caller&&!args[4].equals("receiver"))throw new IOException("role");
    RANDOM.nextLong();com.biglybt.core.util.RandomUtils.nextSecureBytes(new byte[20]); // Explicit provider warmup before the root's reference fence.
    final AtomicBoolean done=new AtomicBoolean();final AtomicReference<Throwable> failed=new AtomicReference<>();final AtomicInteger requests=new AtomicInteger(),sent=new AtomicInteger();final DHTTransportContact remote=contact(peer);
    final DatagramSocket socket=new DatagramSocket(null);socket.bind(local);socket.setSoTimeout(100);final InetSocketAddress bound=(InetSocketAddress)socket.getLocalSocketAddress();
    System.setSecurityManager(new OwnedFence(peer));
    DHTLogger logger=(DHTLogger)Proxy.newProxyInstance(OriginalRawWritePeer.class.getClassLoader(),new Class<?>[]{DHTLogger.class},(p,m,a)->defaultValue(m.getReturnType()));
    final DHTTransferHandler handler=new DHTTransferHandler(new DHTTransferHandler.Adapter(){public long getConnectionID(){return RANDOM.nextLong()|Long.MIN_VALUE;}public void sendRequest(DHTTransportContact target,DHTTransferHandler.Packet packet){try{if(!target.getTransportAddress().equals(peer))throw new IOException("foreign target");byte[] bytes=encode(packet,bound,sent.incrementAndGet());socket.send(new DatagramPacket(bytes,bytes.length,peer));}catch(Throwable error){failed.compareAndSet(null,error);done.set(true);}}},1317,1,logger);
    Map<String,Object> options=new HashMap<>();options.put("disable_call_acks",false);handler.registerTransferHandler(HANDLER,new DHTTransportTransferHandler(){public String getName(){return "owned-DATA-original-handler";}public byte[] handleRead(DHTTransportContact originator,byte[] key){return null;}public byte[] handleWrite(DHTTransportContact originator,byte[] key,byte[] value){if(!originator.getTransportAddress().equals(peer)||key.length!=0||!Arrays.equals(value,value(payloadBytes,(byte)'q'))||requests.incrementAndGet()!=1){failed.set(new IOException("actual original request mismatch/duplicate"));return null;}return null;}},options);
    System.out.println("READY "+bound.getPort());System.out.flush();
    Thread call=null;if(caller){if(!"GO".equals(new BufferedReader(new InputStreamReader(System.in,"UTF-8")).readLine()))throw new IOException("owned trigger");call=new Thread(()->{try{handler.writeTransfer(null,remote,HANDLER,new byte[0],value(payloadBytes,(byte)'q'),10000);System.out.println("COMPLETE caller transport-ack-only sent="+sent.get());done.set(true);}catch(Throwable error){failed.set(error);done.set(true);}},"owned-original-call");call.setDaemon(true);call.start();}
    long end=System.nanoTime()+12_000_000_000L,receivedAt=0;byte[] buffer=new byte[1431];while(!done.get()&&System.nanoTime()<end){try{DatagramPacket datagram=new DatagramPacket(buffer,buffer.length);socket.receive(datagram);if(!datagram.getSocketAddress().equals(peer)||datagram.getLength()>1430)throw new IOException("foreign/oversize");handler.receivePacket(remote,decode(buffer,datagram.getLength(),peer));if(!caller&&requests.get()==1){if(receivedAt==0)receivedAt=System.nanoTime();if(System.nanoTime()-receivedAt>1_000_000_000L){done.set(true);}}}catch(SocketTimeoutException timeout){if(!caller&&requests.get()==1){if(receivedAt==0)receivedAt=System.nanoTime();if(System.nanoTime()-receivedAt>1_000_000_000L)done.set(true);}}}
    socket.close();if(call!=null)call.join(1000);if(failed.get()!=null)throw new IOException("original DATA",failed.get());if(!done.get())throw new IOException("absolute fixture timeout");if(!caller)System.out.println("COMPLETE receiver requests="+requests.get()+" sent="+sent.get());
  }
}
