// Offline actual I2P objects only. Synthetic identity/lease keys, no session,
// router, connect, profile or default files. Java8 source; root runs JDK17.
import java.io.*;
import java.nio.file.*;
import java.nio.file.attribute.PosixFilePermissions;
import java.util.*;
import net.i2p.crypto.*;
import net.i2p.data.*;
import net.i2p.data.i2cp.*;

public final class LeaseSetEnc4Vectors {
  static final String[] LABELS={"dsa-one","dsa-sixteen","ed25519-one","ed25519-sixteen"};
  static void require(boolean ok,String why){if(!ok)throw new IllegalStateException(why);}
  static byte[] read(Path p)throws Exception{byte[] b=Files.readAllBytes(p);require(b.length<=4096,"Unbounded vector");return b;}
  static void write(Path root,String name,byte[] b)throws Exception{require(b.length<=4096,"Unbounded output");Files.write(root.resolve(name),b,StandardOpenOption.CREATE_NEW);Files.setPosixFilePermissions(root.resolve(name),PosixFilePermissions.fromString("rw-------"));}
  static byte[] serialize(I2CPMessage m)throws Exception{ByteArrayOutputStream out=new ByteArrayOutputStream();m.writeMessage(out);return out.toByteArray();}
  static I2CPMessage parse(byte[] b)throws Exception{ByteArrayInputStream in=new ByteArrayInputStream(b);I2CPMessage m=I2CPMessageHandler.readMessage(in);require(in.available()==0,"Trailing I2CP bytes");return m;}
  static Destination destination(Path p)throws Exception{ByteArrayInputStream in=new ByteArrayInputStream(read(p));Destination d=Destination.create(in);require(in.available()==0,"Trailing destination bytes");return d;}
  static RequestVariableLeaseSetMessage request(int n,long now){RequestVariableLeaseSetMessage m=new RequestVariableLeaseSetMessage();m.setSessionId(new SessionId(7));for(int i=0;i<n;i++){Lease l=new Lease();byte[] hash=new byte[32];Arrays.fill(hash,(byte)(i+1));l.setGateway(new Hash(hash));l.setTunnelId(new TunnelId(100+i));l.setEndDate(now+600001+i*1000);m.addEndpoint(l);}return m;}
  static LeaseSet2 leaseSet(Destination dest,SigningPrivateKey signer,PrivateKey key,RequestVariableLeaseSetMessage req,long now,boolean unpublished)throws Exception{
    LeaseSet2 ls=new LeaseSet2();ls.setDestination(dest);ls.setPublished(now);if(unpublished)ls.setUnpublished();ls.addEncryptionKey(KeyGenerator.getPublicKey(key));
    for(int i=0;i<req.getEndpoints();i++){Lease l=req.getEndpoint(i);Lease2 copy=new Lease2();copy.setGateway(l.getGateway());copy.setTunnelId(l.getTunnelId());copy.setEndDate(l.getEndTime());ls.addLease(copy);}ls.sign(signer);require(ls.verifySignature(),"Original LS2 signing failed");return ls;
  }
  static CreateLeaseSet2Message create(LeaseSet2 ls,PrivateKey key){CreateLeaseSet2Message m=new CreateLeaseSet2Message();m.setSessionId(new SessionId(7));m.setLeaseSet(ls);m.addPrivateKey(key);return m;}
  static void produce(Path root,Path fixtures)throws Exception{
    Files.createDirectory(root,PosixFilePermissions.asFileAttribute(PosixFilePermissions.fromString("rwx------")));long now=System.currentTimeMillis();write(root,"creation.clock",DataHelper.toLong(8,now));
    for(String family:new String[]{"dsa","ed25519"}){write(root,family+".sender",read(fixtures.resolve(family+".sender")));write(root,family+".known-test-key",read(fixtures.resolve(family+".known-test-key")));}
    for(int row=0;row<LABELS.length;row++){
      String label=LABELS[row],family=row<2?"dsa":"ed25519";int n=row%2==0?1:16;Destination dest=destination(root.resolve(family+".sender"));SigningPrivateKey signer=new SigningPrivateKey(row<2?SigType.DSA_SHA1:SigType.EdDSA_SHA512_Ed25519,read(root.resolve(family+".known-test-key")));
      // Synthetic raw X25519 clamp-edge variants, independent of identity bytes.
      byte[] secret=new byte[32];for(int i=0;i<secret.length;i++)secret[i]=(byte)(i*7+1);secret[0]^=(byte)row;secret[31]^=(byte)((row&3)<<6);PrivateKey key=new PrivateKey(EncType.ECIES_X25519,secret);
      RequestVariableLeaseSetMessage req=request(n,now);write(root,label+".java.request",serialize(req));write(root,label+".java.create",serialize(create(leaseSet(dest,signer,key,req,now,n==16),key)));
      CreateLeaseSet2Message parsed=(CreateLeaseSet2Message)parse(read(root.resolve(label+".java.create")));require(parsed.getLeaseSet().verifySignature(),"Original LS2 readback failed");System.out.println("PASS JAVA PRODUCED "+label+" request create");
    }
  }
  static void verify(Path root)throws Exception{
    long now=DataHelper.fromLong(read(root.resolve("creation.clock")),0,8);
    for(int row=0;row<LABELS.length;row++){
      String label=LABELS[row],family=row<2?"dsa":"ed25519";int n=row%2==0?1:16;
      I2CPMessage parsed=parse(read(root.resolve(label+".native.create")));require(parsed instanceof CreateLeaseSet2Message,"Native type differs");CreateLeaseSet2Message m=(CreateLeaseSet2Message)parsed;require(m.getSessionId().getSessionId()==7&&m.getLeaseSet() instanceof LeaseSet2,"Native session/LS2 type differs");LeaseSet2 ls=(LeaseSet2)m.getLeaseSet();
      require(ls.verifySignature()&&ls.getDestination().equals(destination(root.resolve(family+".sender"))),"Native signature/destination differs");require(ls.getPublished()==((now+500)/1000)*1000&&ls.getLeaseCount()==n&&ls.isUnpublished()==(n==16),"Native time/count/flags differ");
      RequestVariableLeaseSetMessage req=request(n,now);long latest=0;for(int i=0;i<n;i++){Lease actual=ls.getLease(i),expected=req.getEndpoint(i);require(actual.getGateway().equals(expected.getGateway())&&actual.getTunnelId().equals(expected.getTunnelId())&&actual.getEndTime()==(expected.getEndTime()/1000)*1000,"Native lease projection differs");latest=Math.max(latest,actual.getEndTime());}require(ls.getExpires()==latest,"Native latest expiry differs");
      require(m.getPrivateKeys().size()==1&&m.getPrivateKeys().get(0).getType()==EncType.ECIES_X25519&&KeyGenerator.getPublicKey(m.getPrivateKeys().get(0)).equals(ls.getEncryptionKey()),"Native independent keypair does not derive");require(Arrays.equals(serialize(m),read(root.resolve(label+".native.create"))),"Original reserialization changed native canonical frame");System.out.println("PASS JAVA VERIFIED NATIVE "+label+" create");
    }
    CreateLeaseSet2Message changed=(CreateLeaseSet2Message)parse(read(root.resolve("native.bad-signature.create")));require(!changed.getLeaseSet().verifySignature(),"Original accepted changed signature");System.out.println("PASS JAVA REFUSED CHANGED LEASESET SIGNATURE");
  }
  public static void main(String[] args)throws Exception{
    require(args.length>=3,"produce ROOT FIXTURES JAR | verify ROOT JAR");Path root=Paths.get(args[1]);require(root.isAbsolute(),"Absolute root required");
    java.net.URI expected=Paths.get(args[args.length-1]).toRealPath().toUri();
    for(Class<?> c:new Class<?>[]{LeaseSet2.class,KeyGenerator.class,CreateLeaseSet2Message.class,RequestVariableLeaseSetMessage.class,Destination.class})require(c.getProtectionDomain().getCodeSource().getLocation().toURI().equals(expected),"Wrong reference class origin: "+c.getName());
    System.out.println("PASS EXACT REFERENCE CLASS ORIGINS");
    if(args[0].equals("produce")){require(args.length==4,"Fixture input required");produce(root,Paths.get(args[2]));}else{require(args[0].equals("verify")&&args.length==3,"Unknown mode");verify(root);}
  }
}
