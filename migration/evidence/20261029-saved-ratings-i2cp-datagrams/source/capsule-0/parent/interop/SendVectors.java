// Offline original message objects + datagrams, no router/session.connect.
import java.io.*;
import java.lang.reflect.Proxy;
import java.nio.file.*;
import java.nio.file.attribute.PosixFilePermissions;
import java.util.*;
import net.i2p.I2PAppContext;
import net.i2p.client.I2PSession;
import net.i2p.client.datagram.*;
import net.i2p.crypto.SigType;
import net.i2p.data.*;
import net.i2p.data.i2cp.*;
public final class SendVectors {
  static final byte[] PAYLOAD={0,1,(byte)255,0x51,0,(byte)128,0};
  static final int[] CODES={1,4,6,15};
  static void require(boolean ok,String why){if(!ok)throw new IllegalStateException(why);}
  static byte[] read(Path path)throws Exception{byte[] bytes=Files.readAllBytes(path);require(bytes.length<=70000,"Unbounded vector");return bytes;}
  static void write(Path root,String name,byte[] bytes)throws Exception{require(bytes.length<=70000,"Unbounded output");Files.write(root.resolve(name),bytes,StandardOpenOption.CREATE_NEW);Files.setPosixFilePermissions(root.resolve(name),PosixFilePermissions.fromString("rw-------"));}
  static Destination dest(byte[] raw)throws Exception{ByteArrayInputStream in=new ByteArrayInputStream(raw);Destination d=Destination.create(in);require(in.available()==0,"Trailing Destination");return d;}
  static I2PSession boundary(final Destination dest,final SigningPrivateKey key){return (I2PSession)Proxy.newProxyInstance(I2PSession.class.getClassLoader(),new Class<?>[]{I2PSession.class},(proxy,method,args)->{switch(method.getName()){case "getMyDestination":return dest;case "getPrivateKey":return key;case "isOffline":return false;default:throw new UnsupportedOperationException("Unexpected inert boundary: "+method.getName());}});}
  static byte[] frame(I2CPMessage m)throws Exception{ByteArrayOutputStream out=new ByteArrayOutputStream();m.writeMessage(out);return out.toByteArray();}
  static I2CPMessage parse(byte[] raw)throws Exception{ByteArrayInputStream in=new ByteArrayInputStream(raw);I2CPMessage m=I2CPMessageHandler.readMessage(in);require(in.available()==0,"Trailing I2CP bytes");return m;}
  static byte[] mux(byte[] datagram,int protocol,boolean compressed){byte[] out=DataHelper.compress(datagram,0,datagram.length,compressed?3:0);out[4]=(byte)255;out[5]=(byte)254;out[6]=0;out[7]=42;out[9]=(byte)protocol;return out;}
  static String label(String family,boolean claimed,boolean compressed){return family+"-"+(claimed?"3":"2")+"-"+(compressed?"deflate":"stored");}
  static Payload payload(byte[] raw){Payload p=new Payload();p.setEncryptedData(raw);return p;}
  static void produce(Path root,Path fixtures)throws Exception{
    Files.createDirectory(root,PosixFilePermissions.asFileAttribute(PosixFilePermissions.fromString("rwx------")));write(root,"payload",PAYLOAD);
    Properties properties=new Properties();properties.setProperty("i2p.dir.base",root.toString());properties.setProperty("i2p.dir.config",root.toString());I2PAppContext ctx=new I2PAppContext(properties);
    for(String family:new String[]{"dsa","ed25519"}){
      byte[] from=read(fixtures.resolve(family+".sender")),to=read(fixtures.resolve(family+".recipient")),secret=read(fixtures.resolve(family+".known-test-key"));write(root,family+".sender",from);write(root,family+".recipient",to);write(root,family+".known-test-key",secret);
      Destination sender=dest(from),recipient=dest(to);I2PSession source=boundary(sender,new SigningPrivateKey(family.equals("dsa")?SigType.DSA_SHA1:SigType.EdDSA_SHA512_Ed25519,secret));
      for(boolean claimed:new boolean[]{false,true})for(boolean compressed:new boolean[]{false,true}){
        String label=label(family,claimed,compressed);byte[] packet=claimed?Datagram3.make(ctx,source,PAYLOAD):Datagram2.make(ctx,source,PAYLOAD,recipient.calculateHash());Payload payload=payload(mux(packet,claimed?20:19,compressed));SendMessageMessage m;
        if(compressed){SendMessageExpiresMessage expires=new SendMessageExpiresMessage(new SessionId(7),recipient,payload,0x80000000L);expires.setExpiration(1700000030123L);m=expires;}else m=new SendMessageMessage(new SessionId(7),recipient,payload,0x80000000L);
        write(root,label+".java.send",frame(m));System.out.println("PASS JAVA PRODUCED "+label+" send");
      }
    }
    for(int code:CODES){MessageStatusMessage m=new MessageStatusMessage();m.setSessionId(7);m.setMessageId(0xffffffffL);m.setNonce(0x80000000L);m.setSize(65536);m.setStatus(code);write(root,"status"+code+".java",frame(m));}
    byte[] wire=((SendMessageMessage)parse(read(root.resolve("dsa-2-stored.java.send")))).getPayload().getEncryptedData();write(root,"payload.java",frame(new MessagePayloadMessage(7,0xffffffffL,payload(wire))));
  }
  static void verify(Path root)throws Exception{
    I2PAppContext ctx=new I2PAppContext(new Properties());
    for(String family:new String[]{"dsa","ed25519"})for(boolean claimed:new boolean[]{false,true})for(boolean compressed:new boolean[]{false,true}){
      String label=label(family,claimed,compressed);SendMessageMessage m=(SendMessageMessage)parse(read(root.resolve(label+".native.send")));Destination sender=dest(read(root.resolve(family+".sender"))),recipient=dest(read(root.resolve(family+".recipient")));require(m.getSessionId().getSessionId()==7&&m.getNonce()==0x80000000L&&m.getDestination().equals(recipient),"Native full Send fields differ");require((m instanceof SendMessageExpiresMessage)==compressed,"Native message type differs");if(compressed){require(((SendMessageExpiresMessage)m).getExpirationTime()==1700000030123L&&((SendMessageExpiresMessage)m).getFlags()==0,"Native flags/date differ");}
      byte[] gzip=m.getPayload().getEncryptedData();require(gzip[3]==0&&(gzip[4]&255)==255&&(gzip[5]&255)==254&&gzip[6]==0&&gzip[7]==42&&(gzip[9]&255)==(claimed?20:19),"Native mux header differs");byte[] packet=DataHelper.decompress(gzip);I2PSession receiver=boundary(recipient,null);
      if(claimed){Datagram3 d=Datagram3.load(ctx,receiver,packet);require(d.getSender().equals(sender.calculateHash())&&Arrays.equals(d.getPayload(),PAYLOAD),"Native claimed datagram differs");}else{Datagram2 d=Datagram2.load(ctx,receiver,packet);require(d.getSender().equals(sender)&&Arrays.equals(d.getPayload(),PAYLOAD),"Native signed datagram differs");try{Datagram2.load(ctx,boundary(sender,null),packet);throw new IllegalStateException("Wrong recipient accepted");}catch(I2PInvalidDatagramException expected){}}
      System.out.println("PASS JAVA VERIFIED NATIVE "+label+" send");
    }
    for(int code:CODES){MessageStatusMessage m=(MessageStatusMessage)parse(read(root.resolve("status"+code+".native")));require(m.getSessionId()==7&&m.getMessageId()==0xffffffffL&&m.getNonce()==0x80000000L&&m.getSize()==65536&&m.getStatus()==code,"Native Status22 fields differ");}
    MessagePayloadMessage payload=(MessagePayloadMessage)parse(read(root.resolve("payload.native")));require(payload.getSessionId()==7&&payload.getMessageId()==0xffffffffL&&Arrays.equals(payload.getPayload().getEncryptedData(),((MessagePayloadMessage)parse(read(root.resolve("payload.java")))).getPayload().getEncryptedData()),"Native Payload31 differs");System.out.println("PASS JAVA VERIFIED NATIVE FOUR STATUSES AND INBOUND PAYLOAD");
  }
  public static void main(String[] args)throws Exception{require(args.length>=2,"produce ROOT FIXTURES | verify ROOT");Path root=Paths.get(args[1]);require(root.isAbsolute(),"Absolute owned root required");if(args[0].equals("produce")){require(args.length==3,"Fixture directory required");produce(root,Paths.get(args[2]));}else{require(args[0].equals("verify")&&args.length==2,"Unknown mode");verify(root);}}
}
