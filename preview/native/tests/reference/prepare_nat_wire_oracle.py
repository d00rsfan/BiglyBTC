# SPDX-License-Identifier: GPL-2.0-or-later
"""Extract exact original NAT wire builders; compile original contact serializer.

Pure stdio, no sockets/profile/desktop. Only map construction and contact/key
format are covered, not the original asynchronous puncher or router lifecycle.
"""
import argparse,hashlib,json,re,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--original',required=True);p.add_argument('--classes',required=True);p.add_argument('--javac',required=True);p.add_argument('--output',required=True);a=p.parse_args()
root=Path(a.original);out=Path(a.output);out.mkdir(parents=True,exist_ok=True)
path=root/'core/src/com/biglybt/core/dht/nat/impl/DHTNATPuncherImpl.java';source=path.read_text()
mask=re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"',lambda m:' '*len(m[0]),source,flags=re.S)
def method(name):
 m=re.search(r'(?:protected|private)\s+(?:static\s+)?(?:byte\[\]|Map|boolean|int)\s+'+name+r'\s*\(',source);assert m,name
 begin=m.start();brace=mask.index('{',m.end());depth=0
 for i in range(brace,len(mask)):
  if mask[i]=='{':depth+=1
  elif mask[i]=='}':
   depth-=1
   if depth==0:return source[begin:i+1]
 raise ValueError(name)
names=['getPublishKey','encodeContact','encodePublishValue'];full={name:method(name) for name in names};prefix={}
for name in ['sendBind','sendPunch','sendConnect','sendQuery','sendClose','sendTunnelInbound','sendTunnelOutbound']:
 text=method(name);m=re.search(r'Map\s+(request|message)\s*=\s*new HashMap\(\);',text);assert m,name
 end=text.index('Map response',m.end()) if m[1]=='request' else text.index('return( sendTunnelMessage',m.end());prefix[name]=text[m.start():end]
head="""// Generated from pinned original NAT source. No network or profile.
package com.biglybt.core.dht.transport.udp.impl;
import java.io.*;import java.net.*;import java.lang.reflect.Proxy;import java.security.*;import java.util.*;
import com.biglybt.core.util.BEncoder;
public final class NatWireOracle {
 static final int RT_BIND_REQUEST=0,RT_PUNCH_REQUEST=2,RT_CONNECT_REQUEST=4,RT_TUNNEL_INBOUND=6,RT_TUNNEL_OUTBOUND=7,RT_QUERY_REQUEST=8,RT_CLOSE_REQUEST=10;
 interface DHTTransportUDP {} static final DHTTransportUDP TRANSPORT=new DHTTransportUDP(){};
 static final class DHTTransportException extends Exception {DHTTransportException(String s){super(s);}}
 interface DHTTransportContact {Object getTransport();InetSocketAddress getAddress();InetSocketAddress getExternalAddress();byte getProtocolVersion();byte[] getID();void exportContact(DataOutputStream out)throws Exception;}
 interface DHTTransportUDPContact extends DHTTransportContact {InetSocketAddress getTransportAddress();}
 static DHTTransportContact contact(byte[] raw,int port,byte version)throws Exception {
  InetSocketAddress external=new InetSocketAddress(InetAddress.getByAddress(raw),port),transport=new InetSocketAddress(external.getAddress(),port==65535?65534:port+1);
  return new DHTTransportUDPContact(){public Object getTransport(){return TRANSPORT;}public InetSocketAddress getAddress(){return external;}public InetSocketAddress getExternalAddress(){return external;}public InetSocketAddress getTransportAddress(){return transport;}public byte getProtocolVersion(){return version;}public byte[] getID(){byte[] id=new byte[20];for(int i=0;i<20;i++)id[i]=(byte)i;return id;}public void exportContact(DataOutputStream out)throws Exception{WireUtils.serialiseContact(out,this);}};
 }
 static final class Fence extends SecurityManager {
  final List<String> reads=new ArrayList<>();Fence(){for(String part:System.getProperty("java.class.path").split(File.pathSeparator))reads.add(new File(part).getAbsolutePath());reads.add(new File(System.getProperty("java.home")).getAbsolutePath());}
  public void checkPermission(Permission p){if(p instanceof RuntimePermission&&p.getName().equals("setSecurityManager"))throw new SecurityException("fixed fence");}
  public void checkRead(String file){String full=new File(file).getAbsolutePath();for(String root:reads)if(full.equals(root)||full.startsWith(root+File.separator))return;throw new SecurityException("undeclared read");}
  public void checkWrite(String f){throw new SecurityException("no writes");}public void checkDelete(String f){throw new SecurityException("no deletes");}public void checkConnect(String h,int p){throw new SecurityException("no network");}public void checkAccept(String h,int p){throw new SecurityException("no network");}public void checkListen(int p){throw new SecurityException("no listener");}public void checkExec(String c){throw new SecurityException("no children");}
 }
 static final class Puncher {
  void log(Throwable t){throw new IllegalStateException(t);}
"""
builders=[]
for name,text in prefix.items():
 key='message' if 'Tunnel' in name else 'request'
 builders.append('Map '+name+'(DHTTransportContact target,DHTTransportContact originator,Map originator_client_data,boolean no_tunnel)throws Exception{\n'+text+'\nreturn '+key+';\n}')
foot="""
 }
 public static void main(String[] args)throws Exception{
  System.setSecurityManager(new Fence());Puncher p=new Puncher();
  BufferedReader in=new BufferedReader(new InputStreamReader(System.in,java.nio.charset.StandardCharsets.US_ASCII));for(String line;(line=in.readLine())!=null;){try{
   if(line.length()>1024)throw new IOException("bound");String[] f=line.split(" ");DHTTransportContact c=contact(HexFormat.of().parseHex(f[1]),Integer.parseInt(f[2]),Byte.parseByte(f[3]));byte[] result;
   switch(f[0]){case "CONTACT":result=p.encodeContact(c);break;case "PUBLISH":result=p.encodePublishValue(c);break;case "KEY":result=p.getPublishKey(c);break;case "SOCKET":result=c.getAddress().toString().getBytes(java.nio.charset.StandardCharsets.UTF_8);break;
    case "MAP":{int type=Integer.parseInt(f[4]),mode=Integer.parseInt(f[5]);Map data=mode==0?null:new HashMap();if(data!=null)data.put("kind",new byte[]{'x'});Map map;
     switch(type){case 0:map=p.sendBind(c,c,data,false);break;case 2:map=p.sendPunch(c,c,data,mode==2);break;case 4:map=p.sendConnect(c,c,data,false);break;case 6:map=p.sendTunnelInbound(c,c,data,false);break;case 7:map=p.sendTunnelOutbound(c,c,data,false);break;case 8:map=p.sendQuery(c,c,data,false);break;case 10:map=p.sendClose(c,c,data,false);break;default:throw new IOException("type");}result=BEncoder.encode(map);break;}
    default:throw new IOException("command");}
   System.out.println(HexFormat.of().formatHex(result));
  }catch(Throwable e){System.out.println("ERROR "+e.getClass().getSimpleName());}System.out.flush();}
 }
}
"""
utils=root/'core/src/com/biglybt/core/dht/transport/udp/impl/DHTUDPUtils.java';utext=utils.read_text();umask=re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"',lambda m:' '*len(m[0]),utext,flags=re.S);utility=[]
for name in ['serialiseLength','serialiseByteArray','serialiseAddress','serialiseContact']:
 for m in re.finditer(r'protected\s+static\s+void\s+'+name+r'\s*\(',utext):
  brace=umask.index('{',m.end());depth=0
  for i in range(brace,len(umask)):
   if umask[i]=='{':depth+=1
   elif umask[i]=='}':
    depth-=1
    if depth==0:utility.append(utext[m.start():i+1]);break
assert len(utility)==5
seam='\n static final class WireUtils {static final int CT_UDP=1;\n'+'\n'.join(utility)+'\n }\n'
java=out/'NatWireOracle.java';java.write_text(head+'\n'.join(full.values())+'\n'+'\n'.join(builders)+foot.replace('\n }\n public static void main','\n }\n'+seam+' public static void main',1))
subprocess.run([a.javac,'--release','17','-cp',a.classes,'-d',str(out),str(java)],check=True)
sha=lambda v:hashlib.sha256(v).hexdigest()
(out/'nat-source-scope.json').write_text(json.dumps({'original':'cb8c6cb9bb97916cbc7e80338a017200ffc3e71f','puncher_sha256':sha(path.read_bytes()),'utils_sha256':sha(utils.read_bytes()),'exact_utility_methods':[sha(v.encode()) for v in utility],'exact_methods':{k:sha(v.encode()) for k,v in full.items()},'exact_map_builder_prefixes':{k:sha(v.encode()) for k,v in prefix.items()},'scope':'Exact original contact/key/publication methods and send-map construction blocks; exact selected DHTUDPUtils contact/address/byte-array writer bodies. Generated memory contact/transport interfaces and CT_UDP1, original BEncoder; pure stdio. Original full UDP interface/utility initialisers excluded to avoid global profile/security side effects. No original receive/timer/NAT/router/owner lifecycle equivalence claimed.'},indent=2)+'\n')
print('PASS original NAT wire oracle prepared')
