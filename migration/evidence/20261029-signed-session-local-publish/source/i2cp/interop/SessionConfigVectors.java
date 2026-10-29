// Test-only actual message-object interop. Synthetic keys only; no connect/router.
import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.nio.ByteBuffer;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.StandardOpenOption;
import java.nio.file.attribute.PosixFilePermissions;
import java.util.Arrays;
import java.util.Date;
import java.util.Properties;
import net.i2p.crypto.DSAEngine;
import net.i2p.crypto.SigType;
import net.i2p.data.DataHelper;
import net.i2p.data.Destination;
import net.i2p.data.SigningPrivateKey;
import net.i2p.data.i2cp.CreateSessionMessage;
import net.i2p.data.i2cp.I2CPMessage;
import net.i2p.data.i2cp.I2CPMessageHandler;
import net.i2p.data.i2cp.ReconfigureSessionMessage;
import net.i2p.data.i2cp.SessionConfig;
import net.i2p.data.i2cp.SessionId;

public final class SessionConfigVectors {
  private static final String[] LABELS={"dsa-empty","dsa-utf8","dsa-maximum","ed25519-empty","ed25519-utf8","ed25519-maximum"};
  private static void require(boolean ok,String message){if(!ok){throw new IllegalStateException(message);}}
  private static byte[] read(Path path)throws Exception{
    require(Files.isRegularFile(path)&&Files.size(path)<=131077,"Unsafe/oversized fixture "+path.getFileName());
    return Files.readAllBytes(path);
  }
  private static void write(Path root,String leaf,byte[] bytes)throws Exception{
    require(bytes.length<=131077,"Unbounded output");
    Files.write(root.resolve(leaf),bytes,StandardOpenOption.CREATE_NEW,StandardOpenOption.WRITE);
    Files.setPosixFilePermissions(root.resolve(leaf),PosixFilePermissions.fromString("rw-------"));
  }
  private static Properties options(String label){
    Properties out=new Properties();
    if(label.endsWith("-utf8")){
      out.setProperty("z","last");out.setProperty("\uE000","BMP");out.setProperty("\uD800\uDC00","supplementary");
      out.setProperty("inbound.nickname","\u0442\u0443\u043D\u043D\u0435\u043B\u044C");out.setProperty("a","\0=;\n");
    }else if(label.endsWith("-maximum")){
      char[] value=new char[255];Arrays.fill(value,'v');char[] first=new char[252];Arrays.fill(first,'v');out.setProperty("A",new String(first));
      for(int i=0;i<127;++i){char[] key=new char[255];Arrays.fill(key,'b');key[252]=(char)('0'+i/100);key[253]=(char)('0'+(i/10)%10);key[254]=(char)('0'+i%10);out.setProperty(new String(key),new String(value));}
    }return out;
  }
  private static Destination destination(byte[] bytes)throws Exception{
    Destination out=new Destination();ByteArrayInputStream input=new ByteArrayInputStream(bytes);out.readBytes(input);require(input.available()==0,"Trailing Destination");return out;
  }
  private static byte[] preimage(SessionConfig config)throws Exception{
    ByteArrayOutputStream out=new ByteArrayOutputStream();config.getDestination().writeBytes(out);
    DataHelper.writeProperties(out,config.getOptions(),true);DataHelper.writeDate(out,config.getCreationDate());return out.toByteArray();
  }
  private static byte[] frame(I2CPMessage message)throws Exception{ByteArrayOutputStream out=new ByteArrayOutputStream();message.writeBytes(out);return out.toByteArray();}
  private static SessionConfig signed(Destination dest,SigningPrivateKey key,Properties props,long creation)throws Exception{
    SessionConfig config=new SessionConfig(dest);config.setOptions(props);config.setCreationDate(new Date(creation));config.signSessionConfig(key);
    require(config.verifySignature(),"Original fresh SessionConfig failed verification");return config;
  }
  private static I2CPMessage parsed(byte[] bytes)throws Exception{
    ByteArrayInputStream in=new ByteArrayInputStream(bytes);I2CPMessage message=I2CPMessageHandler.readMessage(in);require(in.available()==0,"Trailing frame");return message;
  }
  private static SessionConfig config(I2CPMessage message){
    if(message instanceof CreateSessionMessage){return ((CreateSessionMessage)message).getSessionConfig();}
    require(message instanceof ReconfigureSessionMessage,"Unexpected message type");return ((ReconfigureSessionMessage)message).getSessionConfig();
  }
  private static void produce(Path root,Path fixtures)throws Exception{
    require(root.isAbsolute()&&!Files.exists(root),"Fresh absolute fixture root required");
    Files.createDirectory(root,PosixFilePermissions.asFileAttribute(PosixFilePermissions.fromString("rwx------")));
    final long creation=System.currentTimeMillis();write(root,"creation.clock",ByteBuffer.allocate(8).putLong(creation).array());
    for(String family:new String[]{"dsa","ed25519"}){
      write(root,family+".sender",read(fixtures.resolve(family+".sender")));write(root,family+".sign",read(fixtures.resolve(family+".known-test-key")));
    }
    for(int i=0;i<LABELS.length;++i){
      String label=LABELS[i],family=label.startsWith("dsa-")?"dsa":"ed25519";
      Destination dest=destination(read(root.resolve(family+".sender")));SigningPrivateKey key=new SigningPrivateKey(family.equals("dsa")?SigType.DSA_SHA1:SigType.EdDSA_SHA512_Ed25519,read(root.resolve(family+".sign")));
      SessionConfig config=signed(dest,key,options(label),creation);write(root,label+".java.preimage",preimage(config));
      CreateSessionMessage create=new CreateSessionMessage(config);write(root,label+".java.create",frame(create));
      ReconfigureSessionMessage reconfigure=new ReconfigureSessionMessage();reconfigure.setSessionId(new SessionId(i==0?0:i==5?65534:i));reconfigure.setSessionConfig(config);write(root,label+".java.reconfigure",frame(reconfigure));
      require(config(parsed(read(root.resolve(label+".java.create")))).verifySignature(),"Original create readback failed");
      require(config(parsed(read(root.resolve(label+".java.reconfigure")))).verifySignature(),"Original reconfigure readback failed");
      System.out.println("PASS JAVA PRODUCED "+label+" create");System.out.println("PASS JAVA PRODUCED "+label+" reconfigure");
      if(i==0){Properties auth=new Properties();auth.setProperty("i2cp.username","synthetic-only");write(root,"java.unsupported-auth.create",frame(new CreateSessionMessage(signed(dest,key,auth,creation))));}
    }
  }
  private static void verify(Path root)throws Exception{
    final byte[] raw=read(root.resolve("creation.clock"));require(raw.length==8,"Invalid clock");final long creation=ByteBuffer.wrap(raw).getLong();
    for(int i=0;i<LABELS.length;++i){
      String label=LABELS[i],family=label.startsWith("dsa-")?"dsa":"ed25519";Destination expected=destination(read(root.resolve(family+".sender")));
      for(String kind:new String[]{"create","reconfigure"}){
        I2CPMessage message=parsed(read(root.resolve(label+".native."+kind)));SessionConfig config=config(message);
        require(kind.equals("create")?message instanceof CreateSessionMessage:message instanceof ReconfigureSessionMessage,"Native message type differs");
        require(config.getDestination().equals(expected)&&config.getCreationDate().getTime()==creation&&config.getOptions().equals(options(label)),"Native fields differ");
        require(Arrays.equals(preimage(config),read(root.resolve(label+".java.preimage"))),"Native canonical preimage differs");
        require(config.verifySignature(),"Native raw signature/freshness rejected by original");
        if(message instanceof ReconfigureSessionMessage){require(((ReconfigureSessionMessage)message).getSessionId().getSessionId()==(i==0?0:i==5?65534:i),"Native session ID differs");}
        System.out.println("PASS JAVA VERIFIED NATIVE "+label+" "+kind);
      }
    }
    SessionConfig bad=config(parsed(read(root.resolve("native.bad-signature.create"))));require(!bad.verifySignature(),"Original accepted changed native signature");
    SessionConfig stale=config(parsed(read(root.resolve("native.stale.create"))));
    require(stale.tooOld()&&!stale.verifySignature()&&DSAEngine.getInstance().verifySignature(stale.getSignature(),preimage(stale),stale.getDestination().getSigningPublicKey()),"Original date refusal confused with signature failure");
    System.out.println("PASS JAVA REFUSED CHANGED SIGNATURE AND VALIDLY SIGNED STALE DATE");
  }
  public static void main(String[] args)throws Exception{
    require(args.length>=2,"produce ROOT FIXTURES | verify ROOT");Path root=Paths.get(args[1]);
    if(args[0].equals("produce")){require(args.length==3,"Missing fixtures");produce(root,Paths.get(args[2]));}
    else{require(args[0].equals("verify")&&args.length==2,"Invalid mode");verify(root);}
  }
}
