// Test-only original message API bridge. No session, router, socket or connection.
import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.StandardOpenOption;
import java.nio.file.attribute.PosixFilePermissions;
import java.util.Arrays;
import java.util.Date;
import java.util.Properties;
import net.i2p.client.I2PClient;
import net.i2p.data.i2cp.DestroySessionMessage;
import net.i2p.data.i2cp.DisconnectMessage;
import net.i2p.data.i2cp.GetDateMessage;
import net.i2p.data.i2cp.I2CPMessage;
import net.i2p.data.i2cp.I2CPMessageHandler;
import net.i2p.data.i2cp.SessionId;
import net.i2p.data.i2cp.SessionStatusMessage;
import net.i2p.data.i2cp.SetDateMessage;

public final class I2cpVectors {
  private static final String[] NAMES={
    "get-date-none","get-date-empty","get-date-version","get-date-maximum",
    "set-date-legacy","set-date-version","set-date-zero","disconnect-control",
    "disconnect-maximum","status-created","status-refused","destroy-session"
  };
  private static void require(boolean ok,String reason){if(!ok)throw new AssertionError(reason);}
  private static String repeated(char c){char[] chars=new char[255];Arrays.fill(chars,c);return new String(chars);}
  private static I2CPMessage message(int index){
    switch(index){
    case 0:return new GetDateMessage();
    case 1:return new GetDateMessage("");
    case 2:return new GetDateMessage("2.13.0");
    case 3:return new GetDateMessage(repeated('A'));
    case 4:{SetDateMessage value=new SetDateMessage();value.setDate(new Date(0x0102030405060708L));return value;}
    case 5:{SetDateMessage value=new SetDateMessage("2.13.0");value.setDate(new Date(1761739200123L));return value;}
    case 6:{SetDateMessage value=new SetDateMessage("");value.setDate(new Date(0));return value;}
    case 7:{DisconnectMessage value=new DisconnectMessage();value.setReason(new String(new char[]{0,9,65,127}));return value;}
    case 8:{DisconnectMessage value=new DisconnectMessage();value.setReason(repeated('Z'));return value;}
    case 9:{SessionStatusMessage value=new SessionStatusMessage();value.setSessionId(new SessionId(0xabcd));value.setStatus(SessionStatusMessage.STATUS_CREATED);return value;}
    case 10:{SessionStatusMessage value=new SessionStatusMessage();value.setSessionId(new SessionId(65535));value.setStatus(SessionStatusMessage.STATUS_REFUSED);return value;}
    case 11:{DestroySessionMessage value=new DestroySessionMessage();value.setSessionId(new SessionId(65535));return value;}
    default:throw new AssertionError("Unknown fixture");
    }
  }
  private static byte[] wire(I2CPMessage message) throws Exception{
    ByteArrayOutputStream output=new ByteArrayOutputStream();message.writeMessage(output);return output.toByteArray();
  }
  private static I2CPMessage parse(byte[] wire) throws Exception{
    ByteArrayInputStream input=new ByteArrayInputStream(wire);I2CPMessage value=I2CPMessageHandler.readMessage(input);
    require(input.available()==0,"Original reader did not consume exact fixture");return value;
  }
  private static boolean equal(Object a,Object b){return a==null?b==null:a.equals(b);}
  private static void fields(I2CPMessage actual,I2CPMessage expected,int index){
    require(actual.getType()==expected.getType(),"Original message type differs");
    if(actual instanceof GetDateMessage){
      GetDateMessage a=(GetDateMessage)actual,b=(GetDateMessage)expected;
      require(equal(a.getVersion(),b.getVersion())&&(a.getOptions()==null||a.getOptions().isEmpty()),"Original GetDate fields differ");
    }else if(actual instanceof SetDateMessage){
      SetDateMessage a=(SetDateMessage)actual,b=(SetDateMessage)expected;
      // DataHelper.readDate(0) is null even when writer was given new Date(0).
      require(index==6?a.getDate()==null:a.getDate()!=null&&a.getDate().equals(b.getDate()),"Original date/null sentinel differs");
      require(equal(a.getVersion(),b.getVersion()),"Original SetDate version differs");
    }else if(actual instanceof DisconnectMessage){
      require(equal(((DisconnectMessage)actual).getReason(),((DisconnectMessage)expected).getReason()),"Original disconnect reason differs");
    }else if(actual instanceof SessionStatusMessage){
      SessionStatusMessage a=(SessionStatusMessage)actual,b=(SessionStatusMessage)expected;
      require(a.getSessionId().equals(b.getSessionId())&&a.getStatus()==b.getStatus(),"Original session status/token differs");
    }else if(actual instanceof DestroySessionMessage){
      require(((DestroySessionMessage)actual).getSessionId().equals(((DestroySessionMessage)expected).getSessionId()),"Original destroy token differs");
    }else throw new AssertionError("Unexpected original message class");
  }
  private static void write(Path root,String name,byte[] value) throws Exception{
    require(value.length<=131077,"Oversized synthetic fixture");Files.write(root.resolve(name),value,StandardOpenOption.CREATE_NEW,StandardOpenOption.WRITE);
  }
  private static byte[] read(Path root,String name) throws Exception{
    Path path=root.resolve(name);require(!Files.isSymbolicLink(path)&&Files.isRegularFile(path,LinkOption.NOFOLLOW_LINKS)&&Files.size(path)<=131077,"Unsafe/oversized synthetic fixture");
    byte[] value=Files.readAllBytes(path);require(value.length<=131077,"Synthetic fixture grew");return value;
  }
  public static void main(String[] args) throws Exception{
    require(args.length==2&&(args[0].equals("produce")||args[0].equals("verify")),"Usage: produce|verify FRESH_ABSOLUTE_OWNED_ROOT");
    Path root=Paths.get(args[1]);require(root.isAbsolute(),"Use owned absolute path");
    if(args[0].equals("produce"))Files.createDirectory(root,PosixFilePermissions.asFileAttribute(PosixFilePermissions.fromString("rwx------")));
    require(!Files.isSymbolicLink(root)&&Files.isDirectory(root,LinkOption.NOFOLLOW_LINKS)&&Files.getPosixFilePermissions(root).equals(PosixFilePermissions.fromString("rwx------")),"Expected private fixture directory");
    System.setProperty("i2p.dir.base",root.toString());System.setProperty("i2p.dir.config",root.toString());
    if(args[0].equals("produce"))write(root,"greeting.java",new byte[]{(byte)I2PClient.PROTOCOL_BYTE});
    for(int i=0;i<NAMES.length;i++){
      I2CPMessage expected=message(i);byte[] original=wire(expected);
      if(args[0].equals("produce")){fields(parse(original),expected,i);write(root,NAMES[i]+".java",original);}
      else{
        byte[] nativeWire=read(root,NAMES[i]+".native");require(Arrays.equals(nativeWire,original),"Native wire differs from original writer");
        fields(parse(nativeWire),expected,i);
      }
      System.out.println("PASS original I2CP "+args[0]+" "+NAMES[i]+" bytes="+original.length);
    }
    if(args[0].equals("produce")){
      Properties auth=new Properties();auth.setProperty("i2cp.username","known-test-user");auth.setProperty("i2cp.password","known-test-value");
      byte[] unsupported=wire(new GetDateMessage("2.13.0",auth));
      require(((GetDateMessage)parse(unsupported)).getOptions().size()==2,"Original authentication fixture lost options");
      write(root,"unsupported-auth.java",unsupported);
    }
    System.out.println("PASS offline original I2CP message bridge; no session/router");
  }
}
