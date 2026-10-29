// Offline reference fixture: public known synthetic keys only, never a profile.
// Exact original reader/serialization/signing APIs; no connect(), router or I/O transport.
import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.lang.reflect.Proxy;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.StandardOpenOption;
import java.nio.file.attribute.PosixFilePermissions;
import java.util.Arrays;
import java.util.Properties;
import net.i2p.I2PAppContext;
import net.i2p.client.I2PSession;
import net.i2p.client.datagram.Datagram2;
import net.i2p.client.impl.I2PClientImpl;
import net.i2p.client.impl.I2PSessionImpl;
import net.i2p.crypto.SigType;
import net.i2p.data.Destination;
import net.i2p.data.KeyCertificate;
import net.i2p.data.PrivateKey;
import net.i2p.data.SigningPrivateKey;

public final class ClientIdentityVectors {
  private static final String[] CASES={
    "dsa-default","dsa-key-certificate","ed25519-default","ed25519-zero-encryption"
  };
  private static void require(boolean condition,String why) {
    if(!condition)throw new AssertionError(why);
  }
  private static byte[] read(Path root,String leaf) throws Exception {
    Path path=root.resolve(leaf);
    require(!Files.isSymbolicLink(path)&&Files.isRegularFile(path,LinkOption.NOFOLLOW_LINKS),"Expected regular synthetic fixture: "+leaf);
    require(Files.size(path)<=1024,"Synthetic fixture exceeded bound: "+leaf);
    byte[] bytes=Files.readAllBytes(path);
    require(bytes.length<=1024,"Synthetic fixture grew: "+leaf);
    return bytes;
  }
  private static void write(Path root,String leaf,byte[] bytes) throws Exception {
    require(bytes.length<=1024,"Synthetic output exceeded bound");
    Files.write(root.resolve(leaf),bytes,StandardOpenOption.CREATE_NEW,StandardOpenOption.WRITE);
  }
  private static Destination destination(byte[] bytes) throws Exception {
    ByteArrayInputStream input=new ByteArrayInputStream(bytes);
    Destination value=Destination.create(input);
    require(input.available()==0&&Arrays.equals(value.toByteArray(),bytes),"Original Destination roundtrip changed bytes");
    return value;
  }
  private static byte[] stream(Destination destination,byte[] encryptionKey,byte[] signingKey) throws Exception {
    ByteArrayOutputStream output=new ByteArrayOutputStream();
    // The three original I2PClientImpl.createDestination/PrivateKeyFile.write calls.
    destination.writeBytes(output);
    new PrivateKey(encryptionKey).writeBytes(output);
    new SigningPrivateKey(destination.getSigningPublicKey().getType(),signingKey).writeBytes(output);
    return output.toByteArray();
  }
  private static byte[] export(I2PSession session) throws Exception {
    ByteArrayOutputStream output=new ByteArrayOutputStream();
    session.getMyDestination().writeBytes(output);
    session.getDecryptionKey().writeBytes(output);
    session.getPrivateKey().writeBytes(output);
    return output.toByteArray();
  }
  private static I2PSessionImpl imported(I2PAppContext context,byte[] wire,byte[] sender,byte[] encryptionKey,byte[] signingKey) throws Exception {
    ByteArrayInputStream input=new ByteArrayInputStream(wire);
    // The original public factory constructs concrete I2PSessionMuxedImpl;
    // its superclass constructor invokes readDestination without connecting.
    I2PSessionImpl session=(I2PSessionImpl)new I2PClientImpl().createSession(context,input,new Properties());
    try {
      require(input.available()==0,"Original online identity reader left bytes");
      require(session.isClosed()&&!session.isOffline(),"Reference fixture unexpectedly connected/offline");
      require(Arrays.equals(session.getMyDestination().toByteArray(),sender),"Original imported public Destination differs");
      require(Arrays.equals(session.getDecryptionKey().getData(),encryptionKey),"Original imported encryption bytes differ");
      require(Arrays.equals(session.getPrivateKey().getData(),signingKey),"Original imported signing key differs");
      require(Arrays.equals(export(session),wire),"Original key-stream export differs");
      return session;
    }catch(Exception | Error failure){session.destroySession(false);throw failure;}
  }
  private static I2PSession recipient(final Destination destination) {
    // Datagram2.load reads these inert getters only. All parsing/signature work is original.
    return (I2PSession)Proxy.newProxyInstance(I2PSession.class.getClassLoader(),new Class<?>[]{I2PSession.class},(proxy,method,args)->{
      switch(method.getName()) {
      case "getMyDestination":return destination;
      case "getPrivateKey":return null;
      case "isOffline":return false;
      default:throw new UnsupportedOperationException("Unexpected Datagram2 boundary: "+method.getName());
      }
    });
  }
  private static boolean rejects(I2PAppContext context,I2PSession recipient,byte[] packet) throws Exception {
    try{Datagram2.load(context,recipient,packet);return false;}
    catch(net.i2p.client.datagram.I2PInvalidDatagramException expected){return true;}
  }
  public static void main(String[] args) throws Exception {
    require(args.length==3&&(args[0].equals("produce")||args[0].equals("verify")),"Usage: produce|verify RETAINED_PUBLIC_FIXTURES FRESH_OWNED_ROOT");
    Path fixtures=Paths.get(args[1]),root=Paths.get(args[2]);
    require(fixtures.isAbsolute()&&root.isAbsolute(),"Use absolute owned paths");
    if(args[0].equals("produce")){
      Files.createDirectory(root,PosixFilePermissions.asFileAttribute(PosixFilePermissions.fromString("rwx------")));
    }
    require(!Files.isSymbolicLink(root)&&Files.isDirectory(root,LinkOption.NOFOLLOW_LINKS),"Expected real owned fixture directory");
    require(Files.getPosixFilePermissions(root).equals(PosixFilePermissions.fromString("rwx------")),"Expected private owned fixture directory");
    Properties options=new Properties();options.setProperty("i2p.dir.base",root.toString());options.setProperty("i2p.dir.config",root.toString());
    I2PAppContext context=new I2PAppContext(options);
    for(String label:CASES){
      String family=label.startsWith("dsa")?"dsa":"ed25519";
      byte[] sender=read(fixtures,family+".sender"),signingKey=read(fixtures,family+".known-test-key");
      if(label.equals("dsa-key-certificate")){
        Destination original=destination(sender);ByteArrayOutputStream output=new ByteArrayOutputStream();
        original.getPublicKey().writeBytes(output);original.getSigningPublicKey().writeBytes(output);
        new KeyCertificate(SigType.DSA_SHA1).writeBytes(output);sender=output.toByteArray();
      }
      Destination from=destination(sender),to=destination(read(fixtures,family+".recipient"));
      byte[] encryptionKey=new byte[256];
      if(!label.endsWith("zero-encryption"))for(int i=0;i<encryptionKey.length;i++)encryptionKey[i]=(byte)(i*11+9);
      byte[] payload=new byte[]{0,1,(byte)255,81,0,(byte)128,0};
      byte[] expected=stream(from,encryptionKey,signingKey);
      require(expected.length==(label.equals("dsa-default")?663:label.startsWith("dsa")?667:679),"Original identity width changed");
      if(args[0].equals("produce")){
        I2PSessionImpl session=imported(context,expected,sender,encryptionKey,signingKey);
        try{
          write(root,label+".sender",sender);write(root,label+".decrypt",encryptionKey);write(root,label+".sign",signingKey);
          write(root,label+".recipient",to.toByteArray());write(root,label+".payload",payload);
          write(root,label+".java.keys",export(session));
          write(root,label+".java2",Datagram2.make(context,session,payload,to.calculateHash()));
        }finally{session.destroySession(false);}
      }else{
        require(Arrays.equals(read(root,label+".java.keys"),expected),"Java key-stream fixture changed");
        byte[] wire=read(root,label+".native.keys");
        I2PSessionImpl session=imported(context,wire,sender,encryptionKey,signingKey);
        try{
          require(Arrays.equals(wire,expected),"Native identity export differs from actual original");
          write(root,label+".verified.keys",export(session));
          byte[] packet=read(root,label+".native2");
          Datagram2 decoded=Datagram2.load(context,recipient(to),packet);
          require(Arrays.equals(decoded.getSender().toByteArray(),sender)&&Arrays.equals(decoded.getPayload(),payload),"Original failed to verify native imported-key Datagram2");
          require(rejects(context,recipient(from),packet),"Original accepted wrong recipient");
          byte[] changed=packet.clone();changed[changed.length-1]^=1;
          require(rejects(context,recipient(to),changed),"Original accepted changed signature");
          changed=packet.clone();changed[sender.length+2]^=1;
          require(rejects(context,recipient(to),changed),"Original accepted changed payload");
        }finally{session.destroySession(false);}
      }
      System.out.println("PASS original online client identity "+args[0]+" "+label+" bytes="+expected.length);
    }
    System.out.println("PASS offline actual original reader/export/signer; no connected session");
  }
}
