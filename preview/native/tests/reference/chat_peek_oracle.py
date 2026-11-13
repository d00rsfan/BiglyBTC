# Selected original responder and listener bodies; real original bencode.
from pathlib import Path
import argparse,re,json,hashlib
p=argparse.ArgumentParser();p.add_argument('--handler',required=True);p.add_argument('--plugin',required=True);p.add_argument('--output',required=True);a=p.parse_args()
h=Path(a.handler).read_text();s=Path(a.plugin).read_text()
def method(text,start):
 brace=text.index('{',start);depth=0
 for end in range(brace,len(text)):
  if text[end]=='{':depth+=1
  elif text[end]=='}':
   depth-=1
   if depth==0:return text[start:end+1]
 raise ValueError('Unclosed original method')
anchor=h.index('peek_xfer_handler =');start=re.search(r'public\s+byte\[\]\s+handleRead\s*\(',h[anchor:]).start()+anchor;responder=method(h,start)
anchor=s.index('peekMessageHandler(');start=re.search(r'public\s+boolean\s+dataReceived\s*\(',s[anchor:]).start()+anchor;listener=method(s,start)
aliveStart=re.search(r'private\s+void\s+nodeIsAlive\s*\(',h).start();alive=method(h,aliveStart)
tickStart=re.search(r'private\s+void\s+checkLiveNodeBloom\s*\(',h).start();tick=method(h,tickStart)
head=''' // Exact original methods; real bencode, explicit memory crypto/counters/timer.
import java.io.*;import java.util.*;import java.security.*;import com.biglybt.core.util.BEncoder;import com.biglybt.core.util.BDecoder;
public final class ChatPeekOracle {
 static class Fence extends SecurityManager{public void checkPermission(Permission p){}public void checkConnect(String h,int p){throw new SecurityException("No test network");}public void checkListen(int p){throw new SecurityException("No test listener");}public void checkAccept(String h,int p){throw new SecurityException("No test accept");}}
 static class DHTPluginContact{String getAddress(){return "127.0.0.1:41000";}}
 static class MsgSyncHandler{}
 static class MsgSyncNode{byte[] key;String address;String getContactAddress(){return address;}byte[] getPublicKey(){return key;}}
 static class MsgSyncMessage{byte[] signature;MsgSyncNode node;byte[] getSignature(){return signature;}MsgSyncNode getNode(){return node;}}
 static class TimerEvent{}interface TimerEventPerformer{void perform(TimerEvent e);}
 static class SystemTime{static long now=1000;static long getMonotonousTime(){return now;}static long getOffsetTime(long delay){return delay;}}
 static class Sem{void releaseForever(){}}
 static class SimpleTimer{static int calls;static void addEvent(String n,long when,TimerEventPerformer p){if(when!=10000)throw new IllegalStateException("Original grace period");calls++;}}
 static byte[] hex(String s){return s.equals("-")?null:HexFormat.of().parseHex(s);}static String hex(byte[] s){return s==null?"-":HexFormat.of().formatHex(s);}
 byte[] peek_xfer_key;final Object message_lock=new Object();LinkedList<MsgSyncMessage> messages=new LinkedList<>();int total,live,estimate;void updateProtocolCounts(String a){}int[] getNodeCounts(boolean cached){return new int[]{total,live};}int getLiveNodeEstimate(){return estimate;}byte[] generalMessageDecrypt(byte[] b){return b;}byte[] generalMessageEncrypt(byte[] b){return b;}
'''
estimator='\n static class Estimate { com.biglybt.core.util.bloom.BloomFilter live_node_counter_bloom=com.biglybt.core.util.bloom.BloomFilterFactory.createAddOnly(1000);long live_node_counter_bloom_start=SystemTime.now,live_node_counter_last_new;int live_node_estimate;\n'+alive+'\n'+tick+'\n }\n'
mid=''' 
 static class Aggregate{int result_count,max_messages,max_live,max_estimate;Map<String,Object> reply=new HashMap<>();Sem sem=new Sem();
'''
foot='''
 }
 public static void main(String[]args)throws Exception{System.setProperty("user.home",args[0]);System.setProperty("azureus.config.path",args[0]+"/profile");System.setSecurityManager(new Fence());Aggregate aggregate=new Aggregate();Estimate estimate=new Estimate();try(BufferedReader in=new BufferedReader(new InputStreamReader(System.in,java.nio.charset.StandardCharsets.US_ASCII))){for(String line;(line=in.readLine())!=null;){String[] bits=line.split(" ",-1);if(bits[0].equals("E")){SystemTime.now=Long.parseLong(bits[1]);estimate=new Estimate();System.out.println("OK");continue;}if(bits[0].equals("N")){SystemTime.now=Long.parseLong(bits[1]);MsgSyncNode node=new MsgSyncNode();node.address=new String(hex(bits[2]),java.nio.charset.StandardCharsets.US_ASCII);estimate.nodeIsAlive(node);System.out.println(estimate.live_node_estimate);continue;}if(bits[0].equals("T")){SystemTime.now=Long.parseLong(bits[1]);estimate.checkLiveNodeBloom();System.out.println(estimate.live_node_estimate);continue;}if(bits[0].equals("Z")){aggregate=new Aggregate();SimpleTimer.calls=0;System.out.println("OK");continue;}if(bits[0].equals("R")){ChatPeekOracle owner=new ChatPeekOracle();owner.peek_xfer_key=hex(bits[1]);owner.total=Integer.parseInt(bits[4]);owner.live=Integer.parseInt(bits[5]);owner.estimate=Integer.parseInt(bits[6]);for(int i=0;i<Integer.parseInt(bits[3]);i++){MsgSyncMessage msg=new MsgSyncMessage();msg.signature=hex(bits[7]);msg.node=new MsgSyncNode();msg.node.key=hex(bits[8]);owner.messages.add(msg);}System.out.println(hex(owner.handleRead(new DHTPluginContact(),hex(bits[2]))));continue;}if(bits[0].equals("A")){try{boolean more=aggregate.dataReceived(new MsgSyncHandler(),BDecoder.decode(hex(bits[1])));System.out.println((more?1:0)+" "+aggregate.result_count+" "+aggregate.reply.get("m")+" "+aggregate.reply.get("n")+" "+hex((byte[])aggregate.reply.get("s"))+" "+hex((byte[])aggregate.reply.get("p"))+" "+SimpleTimer.calls);}catch(Throwable failure){System.out.println("ERROR");}continue;}throw new IOException("Generated peek command");}}}
}
'''
out=Path(a.output);out.parent.mkdir(parents=True,exist_ok=True);out.write_text(head+responder+estimator+mid+listener+foot);(out.parent/'source-scope.json').write_text(json.dumps({'estimate_alive_sha256':hashlib.sha256(alive.encode()).hexdigest(),'estimate_tick_sha256':hashlib.sha256(tick.encode()).hexdigest(),'responder_symbol':'MsgSyncHandler.peek_xfer_handler.handleRead','listener_symbol':'MsgSyncPlugin.peekMessageHandler dataReceived','handler_sha256':hashlib.sha256(h.encode()).hexdigest(),'plugin_sha256':hashlib.sha256(s.encode()).hexdigest(),'responder_body_sha256':hashlib.sha256(responder.encode()).hexdigest(),'listener_body_sha256':hashlib.sha256(listener.encode()).hexdigest(),'scope':'exact selected original methods and real original BEncoder/BDecoder; identity cipher, supplied message/node counters and memory-only timer/contacts; no original encryption, transport or live scheduler runtime claim'},indent=2)+'\n')
