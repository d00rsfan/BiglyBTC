// Actual offline original receive/control/datagram objects only; no session IO.
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
public final class ReceiveVectors {
  static final byte[] PAYLOAD={0,(byte)255,0x51,3};
  static void require(boolean ok,String why){if(!ok)throw new IllegalStateException(why);}
  static byte[] read(Path p)throws Exception{byte[] b=Files.readAllBytes(p);require(b.length<=70000,"Unbounded vector");return b;}
  static void write(Path root,String leaf,byte[] bytes)throws Exception{require(bytes.length<=70000,"Unbounded output");Files.write(root.resolve(leaf),bytes,StandardOpenOption.CREATE_NEW);Files.setPosixFilePermissions(root.resolve(leaf),PosixFilePermissions.fromString("rw-------"));}
  static Destination dest(byte[] b)throws Exception{ByteArrayInputStream in=new ByteArrayInputStream(b);Destination d=Destination.create(in);require(in.available()==0,"Trailing Destination");return d;}
  static I2PSession boundary(final Destination d,final SigningPrivateKey key){return (I2PSession)Proxy.newProxyInstance(I2PSession.class.getClassLoader(),new Class<?>[]{I2PSession.class},(proxy,method,args)->{switch(method.getName()){case "getMyDestination":return d;case "getPrivateKey":return key;case "isOffline":return false;default:throw new UnsupportedOperationException("Unexpected inert getter: "+method.getName());}});}
  static byte[] frame(I2CPMessage m)throws Exception{ByteArrayOutputStream out=new ByteArrayOutputStream();m.writeMessage(out);return out.toByteArray();}
  static I2CPMessage parse(byte[] b)throws Exception{ByteArrayInputStream in=new ByteArrayInputStream(b);I2CPMessage m=I2CPMessageHandler.readMessage(in);require(in.available()==0,"Trailing I2CP frame");return m;}
  static byte[] mux(byte[] b,int protocol){byte[] gzip=DataHelper.compress(b);gzip[4]=0;gzip[5]=13;gzip[6]=0;gzip[7]=42;gzip[9]=(byte)protocol;return gzip;}
  static void produce(Path root,Path fixtures)throws Exception{
    Files.createDirectory(root,PosixFilePermissions.asFileAttribute(PosixFilePermissions.fromString("rwx------")));I2PAppContext ctx=new I2PAppContext(new Properties());int index=0;
    for(String family:new String[]{"dsa","ed25519"}){
      byte[] from=read(fixtures.resolve(family+".sender")),to=read(fixtures.resolve(family+".recipient")),secret=read(fixtures.resolve(family+".known-test-key"));write(root,family+".sender",from);write(root,family+".recipient",to);write(root,family+".known-test-key",secret);Destination sender=dest(from),recipient=dest(to);I2PSession source=boundary(sender,new SigningPrivateKey(family.equals("dsa")?SigType.DSA_SHA1:SigType.EdDSA_SHA512_Ed25519,secret));
      for(boolean claimed:new boolean[]{false,true}){
        String label=family+"-"+(claimed?"3":"2");long id=0x80000000L+index++;byte[] packet=claimed?Datagram3.make(ctx,source,PAYLOAD):Datagram2.make(ctx,source,PAYLOAD,recipient.calculateHash());Payload payload=new Payload();payload.setEncryptedData(mux(packet,claimed?20:19));
        MessageStatusMessage available=new MessageStatusMessage();available.setSessionId(7);available.setMessageId(id);available.setStatus(0);available.setSize(payload.getSize());available.setNonce(1);write(root,label+".java.available",frame(available));write(root,label+".java.payload",frame(new MessagePayloadMessage(7,id,payload)));
        ReceiveMessageBeginMessage begin=new ReceiveMessageBeginMessage();begin.setSessionId(7);begin.setMessageId(id);ReceiveMessageEndMessage end=new ReceiveMessageEndMessage();end.setSessionId(7);end.setMessageId(id);write(root,label+".java.begin",frame(begin));write(root,label+".java.end",frame(end));System.out.println("PASS JAVA PRODUCED "+label+" receive");
      }
    }
  }
  static void verify(Path root)throws Exception{
    I2PAppContext ctx=new I2PAppContext(new Properties());int index=0;
    for(String family:new String[]{"dsa","ed25519"})for(boolean claimed:new boolean[]{false,true}){
      String label=family+"-"+(claimed?"3":"2");long id=0x80000000L+index++;ReceiveMessageBeginMessage begin=(ReceiveMessageBeginMessage)parse(read(root.resolve(label+".native.begin")));ReceiveMessageEndMessage end=(ReceiveMessageEndMessage)parse(read(root.resolve(label+".native.end")));require(begin.getSessionId()==7&&end.getSessionId()==7&&begin.getMessageId()==id&&end.getMessageId()==id,"Native Begin/End fields differ");
      MessagePayloadMessage payload=(MessagePayloadMessage)parse(read(root.resolve(label+".native.payload")));MessageStatusMessage available=(MessageStatusMessage)parse(read(root.resolve(label+".native.available")));require(payload.getSessionId()==7&&payload.getMessageId()==id&&available.getSessionId()==7&&available.getMessageId()==id&&available.getStatus()==0&&available.getNonce()==1&&available.getSize()==payload.getPayload().getSize(),"Native available/payload fields differ");
      byte[] gzip=payload.getPayload().getEncryptedData();require(gzip[4]==0&&gzip[5]==13&&gzip[6]==0&&gzip[7]==42&&(gzip[9]&255)==(claimed?20:19),"Native mux differs");byte[] packet=DataHelper.decompress(gzip);Destination from=dest(read(root.resolve(family+".sender"))),to=dest(read(root.resolve(family+".recipient")));I2PSession receiver=boundary(to,null);
      if(claimed){Datagram3 d=Datagram3.load(ctx,receiver,packet);require(d.getSender().equals(from.calculateHash())&&Arrays.equals(d.getPayload(),PAYLOAD),"Native claimed Datagram3 differs");}else{Datagram2 d=Datagram2.load(ctx,receiver,packet);require(d.getSender().equals(from)&&Arrays.equals(d.getPayload(),PAYLOAD),"Native verified Datagram2 differs");}
      System.out.println("PASS JAVA VERIFIED NATIVE "+label+" receive");
    }
  }
  public static void main(String[] a)throws Exception{require(a.length>=2,"produce ROOT FIXTURES | verify ROOT");Path root=Paths.get(a[1]);require(root.isAbsolute(),"Absolute root required");if(a[0].equals("produce")){require(a.length==3,"Fixture directory required");produce(root,Paths.get(a[2]));}else{require(a[0].equals("verify")&&a.length==2,"Unknown mode");verify(root);}}
}
