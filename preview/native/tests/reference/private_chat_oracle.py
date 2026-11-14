from pathlib import Path
import argparse,hashlib,json,re
p=argparse.ArgumentParser();p.add_argument('--source',required=True);p.add_argument('--output',required=True);a=p.parse_args();source=Path(a.source).read_text()
def block(name):
 m=re.search(r'private\s+(?:byte\[\]|Map<String,Object>)\s+'+name+r'\(',source);assert m,name
 begin=m.start();brace=source.index('{',begin);depth=0
 for end in range(brace,len(source)):
  if source[end]=='{':depth+=1
  elif source[end]=='}':
   depth-=1
   if depth==0:return source[begin:end+1]
 raise ValueError(name)
names=['getSharedSecret','handleDHRequest','generalMessageEncrypt','generalMessageDecrypt','fixSecret','privateMessageEncrypt','privateMessageDecrypt'];methods=[block(x) for x in names]
head=r"""import java.io.*;import java.util.*;import java.nio.*;import java.nio.file.*;import java.net.*;import java.security.*;import java.security.spec.*;
import javax.crypto.*;import javax.crypto.spec.*;
import com.biglybt.core.security.*;import com.biglybt.core.util.BEncoder;import com.biglybt.core.util.BDecoder;import com.biglybt.core.util.HashWrapper;import com.biglybt.pif.ipc.IPCException;
import org.gudy.bouncycastle.jce.provider.BouncyCastleProvider;
import org.gudy.bouncycastle.crypto.engines.AESFastEngine;import org.gudy.bouncycastle.crypto.modes.CBCBlockCipher;import org.gudy.bouncycastle.crypto.paddings.PaddedBufferedBlockCipher;import org.gudy.bouncycastle.crypto.params.KeyParameter;import org.gudy.bouncycastle.crypto.params.ParametersWithIV;
public final class PrivateChatOracle {
 static final BufferedReader input=new BufferedReader(new InputStreamReader(System.in,java.nio.charset.StandardCharsets.US_ASCII));
 static final PrintStream output=System.out;static boolean accept=true;static MsgSyncHandler child;
 static final class Fence extends SecurityManager {final Path root;Fence(String r){root=Path.of(r).toAbsolutePath();}public void checkPermission(Permission p){if(p instanceof RuntimePermission&&p.getName().equals("setSecurityManager"))throw new SecurityException();}
 public void checkConnect(String h,int p){throw new SecurityException("No oracle network");}public void checkListen(int p){throw new SecurityException("No oracle network");}public void checkAccept(String h,int p){throw new SecurityException("No oracle network");}public void checkExec(String c){throw new SecurityException("No oracle process");}
 public void checkWrite(String p){if(!Path.of(p).toAbsolutePath().normalize().startsWith(root))throw new SecurityException("No original profile writes");}public void checkDelete(String p){checkWrite(p);}public void checkRead(String p){if(Path.of(p).toAbsolutePath().toString().contains("/.biglybt/"))throw new SecurityException("No original profiles");}}
 static byte[] raw(String s){return HexFormat.of().parseHex(s);}static String hex(byte[] v){return HexFormat.of().formatHex(v);}
 static final class RandomUtils{static final SecureRandom rng=new SecureRandom();static void nextSecureBytes(byte[] a){rng.nextBytes(a);}}
 static final class SystemTime{static long getMonotonousTime(){return System.nanoTime()/1000000;}}
 static final class Debug{static String getNestedExceptionMessage(Throwable e){return e.getMessage()==null?e.getClass().getSimpleName():e.getMessage();}static void out(Throwable e){}}
 static final class CoreFactory{static CoreFactory getSingleton(){return new CoreFactory();}CoreFactory getCryptoManager(){return this;}CoreFactory getECCHandler(){return this;}CryptoSTSEngine getSTSEngine(PublicKey p,PrivateKey s)throws Exception{
  var ctor=Class.forName("com.biglybt.core.security.impl.CryptoSTSEngineImpl").getDeclaredConstructor(PublicKey.class,PrivateKey.class);ctor.setAccessible(true);return (CryptoSTSEngine)ctor.newInstance(p,s);}}
 interface DHTPluginProgressListener{void reportSize(long v);void reportCompleteness(int v);void reportActivity(String v);}
 static final class DHTPluginContact{byte[] call(DHTPluginProgressListener ignored,byte[] key,byte[] value,long timeout)throws Exception{output.println("CALL "+hex(value));output.flush();return raw(input.readLine());}
 InetSocketAddress getAddress(){return new InetSocketAddress(InetAddress.getLoopbackAddress(),43123);}Map<String,Object> exportToMap(){return new HashMap<>();}}
 static final class MsgSyncNode{DHTPluginContact getContact(){return new DHTPluginContact();}}
 interface MsgSyncListener{String chatRequested(byte[] pk,MsgSyncHandler handler)throws Exception;}
 static final class MsgSyncPlugin{MsgSyncHandler getSyncHandler(Object dht,MsgSyncHandler parent,byte[] pk,Map<String,Object> contact,byte[] user,byte[] shared){child=new MsgSyncHandler(parent.public_key,parent.private_key,parent.general_secret);child.private_messaging_secret=shared;child.remote=pk;child.user=user;return child;}}
 static final class Bloom{int add(byte[] value){return 1;}}
 static final class MsgSyncHandler{
 final int VERSION=5,RT_DH_REQUEST=2,RT_DH_REPLY=3,AES_BYTES=24;
 final PublicKey public_key;final PrivateKey private_key;final byte[] general_secret;
 byte[] private_messaging_secret,dht_call_key=new byte[20],remote,user;Object dht;boolean destroyed;
 final MsgSyncPlugin plugin=new MsgSyncPlugin();final Map<HashWrapper,Object[]> secret_activities=new HashMap<>();final Bloom secret_activities_bloom=new Bloom();final List<MsgSyncListener> listeners=new ArrayList<>();
 MsgSyncHandler(PublicKey p,PrivateKey s,byte[] general){public_key=p;private_key=s;general_secret=general;listeners.add((pk,h)->{if(!accept)throw new IPCException("Private chat disabled by recipient");return "Generated";});}
 void tryTunnel(MsgSyncNode node,boolean forced,Object call){}void reportInfoText(String key,String value){}void destroy(boolean immediate){destroyed=true;}
"""
foot=r"""
 }
 static MsgSyncHandler parent;
 public static void main(String[] args)throws Exception{
  System.setProperty("user.home",args[0]);System.setProperty("azureus.config.path",args[0]+"/profile");System.setSecurityManager(new Fence(args[0]));Security.addProvider(new BouncyCastleProvider());
  for(String line;(line=input.readLine())!=null;){try{if(line.length()>65536)throw new Exception("Bound");String[] f=line.split(" ");switch(f[0]){
   case "INIT":{KeyPair pair=CryptoECCUtils.createKeys();parent=new MsgSyncHandler(pair.getPublic(),pair.getPrivate(),raw(f[1]));child=null;output.println("IDENTITY "+hex(CryptoECCUtils.keyToRawdata(pair.getPublic())));break;}
   case "ACCEPT":accept=f[1].equals("1");output.println("OK");break;
   case "RESPOND":{byte[] plain=parent.generalMessageDecrypt(raw(f[1]));Map<String,Object> reply=parent.handleDHRequest(new DHTPluginContact(),BDecoder.decode(plain));reply.put("s",1L);reply.put("t",3L);output.println("REPLY "+hex(parent.generalMessageEncrypt(BEncoder.encode(reply))));break;}
   case "BEGIN":{boolean[] fatal={false};byte[] secret=parent.getSharedSecret(new MsgSyncNode(),raw(f[1]),raw(f[2]),fatal);if(secret==null){output.println("FAILED "+(fatal[0]?"1":"0"));}else{child=new MsgSyncHandler(parent.public_key,parent.private_key,parent.general_secret);child.private_messaging_secret=secret;output.println("CONNECTED");}break;}
   case "ENCRYPT":{if(child==null||child.destroyed)throw new Exception("No child");output.println("CIPHER "+hex(child.privateMessageEncrypt(raw(f[1]))));break;}
   case "DECRYPT":{if(child==null||child.destroyed)throw new Exception("No child");byte[] plain=child.privateMessageDecrypt(raw(f[1]));output.println(plain==null?"ERROR":"PLAIN "+hex(plain));break;}
   case "CHILD":{output.println(child==null?"NONE":child.destroyed?"DESTROYED":"CHILD "+hex(child.remote)+" "+hex(child.user));break;}
   default:throw new Exception("Unknown oracle command");}
  }catch(Throwable e){output.println("ERROR "+e.getClass().getSimpleName());}output.flush();}
 }
}
"""
out=Path(a.output);out.parent.mkdir(parents=True,exist_ok=True);out.write_text(head+'\n'.join(methods)+foot)
(out.parent/'source-scope.json').write_text(json.dumps({'original_sha256':hashlib.sha256(source.encode()).hexdigest(),'selected_methods':{n:hashlib.sha256(m.encode()).hexdigest() for n,m in zip(names,methods)},'scope':'Exact original getSharedSecret/handleDHRequest and general/private cipher bodies; unchanged original core CryptoSTSEngineImpl/CryptoECCUtils/BEncoder/BDecoder and vendored BouncyCastle. Generated contact/IPC/listener/child collaborators only, calls via stdio not sockets; no real profile, desktop, DHT or network; no claim on Java actual child lifecycle/rate limiter/timer.'},indent=2)+'\n')
