// Actual offline original receive/control/datagram objects only; no session IO.
import java.io.*;
import java.lang.reflect.Proxy;
import java.nio.file.*;
import java.nio.file.attribute.PosixFilePermissions;
import java.util.*;
import net.i2p.I2PAppContext;
import net.i2p.client.I2PSession;
import net.i2p.client.SendMessageOptions;
import net.i2p.client.datagram.*;
import net.i2p.crypto.SigType;
import net.i2p.data.*;
import net.i2p.data.i2cp.*;
public final class TrackerVectors {
  static final byte[] PAYLOAD={0,(byte)255,0x51,3};
  static void require(boolean ok,String why){if(!ok)throw new IllegalStateException(why);}
  static byte[] read(Path p)throws Exception{byte[] b=Files.readAllBytes(p);require(b.length<=70000,"Unbounded vector");return b;}
  static void write(Path root,String leaf,byte[] bytes)throws Exception{require(bytes.length<=70000,"Unbounded output");Files.write(root.resolve(leaf),bytes,StandardOpenOption.CREATE_NEW);Files.setPosixFilePermissions(root.resolve(leaf),PosixFilePermissions.fromString("rw-------"));}
  static Destination dest(byte[] b)throws Exception{ByteArrayInputStream in=new ByteArrayInputStream(b);Destination d=Destination.create(in);require(in.available()==0,"Trailing Destination");return d;}
  static I2PSession boundary(final Destination d,final SigningPrivateKey key){return (I2PSession)Proxy.newProxyInstance(I2PSession.class.getClassLoader(),new Class<?>[]{I2PSession.class},(proxy,method,args)->{switch(method.getName()){case "getMyDestination":return d;case "getPrivateKey":return key;case "isOffline":return false;default:throw new UnsupportedOperationException("Unexpected inert getter: "+method.getName());}});}
  static byte[] frame(I2CPMessage m)throws Exception{ByteArrayOutputStream out=new ByteArrayOutputStream();m.writeMessage(out);return out.toByteArray();}
  static I2CPMessage parse(byte[] b)throws Exception{ByteArrayInputStream in=new ByteArrayInputStream(b);I2CPMessage m=I2CPMessageHandler.readMessage(in);require(in.available()==0,"Trailing I2CP frame");return m;}
  static byte[] mux(byte[] b,int protocol){byte[] gzip=DataHelper.compress(b);gzip[4]=0;gzip[5]=13;gzip[6]=0;gzip[7]=42;gzip[9]=(byte)protocol;return gzip;}
  static byte[] connect()throws Exception{ByteArrayOutputStream b=new ByteArrayOutputStream();DataOutputStream d=new DataOutputStream(b);d.writeLong(0x41727101980L);d.writeInt(0);d.writeInt(123);return b.toByteArray();}
  static byte[] scrape()throws Exception{ByteArrayOutputStream b=new ByteArrayOutputStream();DataOutputStream d=new DataOutputStream(b);d.writeLong(0x123456789abcdef0L);d.writeInt(2);d.writeInt(124);for(int i=0;i<20;++i)d.writeByte(0x51);return b.toByteArray();}
  static byte[] raw()throws Exception{ByteArrayOutputStream b=new ByteArrayOutputStream();DataOutputStream d=new DataOutputStream(b);for(int n:new int[]{2,124,3,4,5})d.writeInt(n);return b.toByteArray();}
  static byte[] trackerMux(byte[] packet,int proto,int from,int to){byte[] b=DataHelper.compress(packet);b[4]=(byte)(from>>8);b[5]=(byte)from;b[6]=(byte)(to>>8);b[7]=(byte)to;b[9]=(byte)proto;return b;}
  static Payload payload(byte[] b){Payload p=new Payload();p.setEncryptedData(b);return p;}
  static void produce(Path root,Path fixtures)throws Exception{
    Files.createDirectory(root,PosixFilePermissions.asFileAttribute(PosixFilePermissions.fromString("rwx------")));I2PAppContext ctx=new I2PAppContext(new Properties());
    for(String family:new String[]{"dsa","ed25519"}){
      byte[] from=read(fixtures.resolve(family+".sender")),to=read(fixtures.resolve(family+".recipient")),secret=read(fixtures.resolve(family+".known-test-key"));write(root,family+".sender",from);write(root,family+".recipient",to);write(root,family+".known-test-key",secret);Destination sender=dest(from),recipient=dest(to);I2PSession source=boundary(sender,new SigningPrivateKey(family.equals("dsa")?SigType.DSA_SHA1:SigType.EdDSA_SHA512_Ed25519,secret));
      write(root,family+".java.lookup",frame(new HostLookupMessage(new SessionId(7),recipient.calculateHash(),0x80000000L,30000)));write(root,family+".java.reply",frame(new HostReplyMessage(new SessionId(7),recipient,0x80000000L)));
      for(boolean connecting:new boolean[]{true,false}){SendMessageOptions options=new SendMessageOptions();options.setDate(1700000060123L);options.setSendLeaseSet(connecting);byte[] data=connecting?Datagram2.make(ctx,source,connect(),recipient.calculateHash()):Datagram3.make(ctx,source,scrape());SendMessageExpiresMessage m=new SendMessageExpiresMessage(new SessionId(7),recipient,payload(trackerMux(data,connecting?19:20,9000,6969)),0,options);write(root,family+(connecting?".connect":".scrape")+".java",frame(m));}
      write(root,family+".java.raw",frame(new MessagePayloadMessage(7,99,payload(trackerMux(raw(),18,6969,9000)))));System.out.println("PASS JAVA PRODUCED "+family+" tracker objects");
    }
  }
  static void verify(Path root)throws Exception{
    I2PAppContext ctx=new I2PAppContext(new Properties());
    for(String family:new String[]{"dsa","ed25519"}){
      Destination sender=dest(read(root.resolve(family+".sender"))),recipient=dest(read(root.resolve(family+".recipient")));HostLookupMessage q=(HostLookupMessage)parse(read(root.resolve(family+".native.lookup")));HostReplyMessage r=(HostReplyMessage)parse(read(root.resolve(family+".native.reply")));require(q.getSessionId().getSessionId()==7&&q.getReqID()==0x80000000L&&q.getTimeout()==30000&&q.getLookupType()==0&&q.getHash().equals(recipient.calculateHash())&&r.getSessionId().getSessionId()==7&&r.getReqID()==q.getReqID()&&r.getResultCode()==0&&r.getDestination().equals(recipient),"Native lookup/reply differs");
      for(boolean connecting:new boolean[]{true,false}){SendMessageExpiresMessage m=(SendMessageExpiresMessage)parse(read(root.resolve(family+(connecting?".connect":".scrape")+".native")));require(m.getSessionId().getSessionId()==7&&m.getNonce()==0&&m.getExpirationTime()==1700000060123L&&m.getFlags()==(connecting?0:0x100)&&m.getDestination().equals(recipient),"Native Expires/skipLS differs");byte[] gzip=m.getPayload().getEncryptedData();require((gzip[4]&255)==35&&(gzip[5]&255)==40&&(gzip[6]&255)==27&&(gzip[7]&255)==57&&(gzip[9]&255)==(connecting?19:20),"Native tracker ports/protocol differ");byte[] packet=DataHelper.decompress(gzip);I2PSession receiver=boundary(recipient,null);if(connecting){Datagram2 d=Datagram2.load(ctx,receiver,packet);require(d.getSender().equals(sender)&&Arrays.equals(d.getPayload(),connect()),"Native signed connect differs");}else{Datagram3 d=Datagram3.load(ctx,receiver,packet);require(d.getSender().equals(sender.calculateHash())&&Arrays.equals(d.getPayload(),scrape()),"Native claimed scrape differs");}}
      MessagePayloadMessage p=(MessagePayloadMessage)parse(read(root.resolve(family+".native.raw")));byte[] gzip=p.getPayload().getEncryptedData();require(p.getSessionId()==7&&p.getMessageId()==99&&(gzip[9]&255)==18&&(gzip[4]&255)==27&&(gzip[5]&255)==57&&(gzip[6]&255)==35&&(gzip[7]&255)==40&&Arrays.equals(DataHelper.decompress(gzip),raw()),"Native raw reply differs");System.out.println("PASS JAVA VERIFIED NATIVE "+family+" tracker objects");
    }
  }
  public static void main(String[] a)throws Exception{require(a.length>=2,"produce ROOT FIXTURES | verify ROOT");Path root=Paths.get(a[1]);require(root.isAbsolute(),"Absolute root required");if(a[0].equals("produce")){require(a.length==3,"Fixture directory required");produce(root,Paths.get(a[2]));}else{require(a[0].equals("verify")&&a.length==2,"Unknown mode");verify(root);}}
}
