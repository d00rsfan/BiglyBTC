from pathlib import Path
import argparse,re,json,hashlib
p=argparse.ArgumentParser();p.add_argument('--source',required=True);p.add_argument('--output',required=True);a=p.parse_args();source=Path(a.source);text=source.read_text();mask=re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',lambda m:' '*len(m[0]),text,flags=re.S);scope=re.search(r'protected\s+class\s+listenerMapper',text).start();method=re.search(r'public\s+void\s+valueRead\s*\(',text[scope:]).start()+scope;anchor=re.search(r'byte\[\]\s+value\s*=\s*_value\.getValue\(\);',text[method:]).start()+method;start=mask.rfind('{',method,anchor);depth=0;end=None
for i in range(start,len(mask)):
 if mask[i]=='{':depth+=1
 elif mask[i]=='}':
  depth-=1
  if depth==0:end=i;break
assert end and start<anchor<end;body=text[start+1:end]
head='''// Exact selected DDBaseImpl normal-read body; memory-only collaborators.
import java.io.*;import java.util.*;import java.security.*;
public final class DdbNormalValueOracle {
 static final class DHTPluginInterface{static final int FLAG_MULTI_VALUE=4;}
 static final class DistributedDatabaseEvent{static final int ET_VALUE_READ=7;}
 static final class Contact{}
 static final class Value{int flags;byte[] bytes;int getFlags(){return flags;}byte[] getValue(){return bytes;}long getCreationTime(){return 123;}int getVersion(){return 4;}}
 static final class Key{String getDescription(){return "Generated normal read";}}
 static final class dbEvent{byte[] data;dbEvent(int type,Key key,Contact originator,byte[] value,long created,int version){data=value;}dbEvent(int type,Key key,Contact originator,Value value){data=value.bytes;}}
 interface Listener{void event(dbEvent event);}
 static final class Debug{static int logs;static void out(String text){logs++;}}
 static final class SHA1Simple{byte[] calculateHash(byte[] value){try{return MessageDigest.getInstance("SHA-1").digest(value);}catch(Exception e){throw new IllegalStateException(e);}}}
 static final class Dht{byte[] next;void get(byte[] key,String description,byte flags,int maximum,long timeout,boolean exhaustive,boolean priority,listenerMapper mapper){if(flags!=0||maximum!=256||timeout!=30000||exhaustive||!priority)throw new IllegalStateException("Original continuation options");next=key;}}
 static final class listenerMapper{
  Listener listener;int type;Key key;byte[] key_bytes;long timeout;int continuation_num;boolean exhaustive=false,high_priority=true,complete_disabled=false;Dht dht=new Dht();
  listenerMapper(Listener l,int t,Key k,byte[] bytes,long time,int number){listener=l;type=t;key=k;key_bytes=bytes;timeout=time;continuation_num=number;}
  Dht grabDHT(){return dht;}
  void valueRead(Contact originator,Value _value){
'''
foot='''
  }
 }
 public static void main(String[] args)throws Exception{try(BufferedReader input=new BufferedReader(new InputStreamReader(System.in,java.nio.charset.StandardCharsets.US_ASCII))){for(String line;(line=input.readLine())!=null;){String[] parts=line.split("\\t",-1);Value value=new Value();value.flags=Integer.parseInt(parts[0]);value.bytes=HexFormat.of().parseHex(parts[1]);byte[] key=HexFormat.of().parseHex(parts[2]);List<byte[]> events=new ArrayList<>();listenerMapper mapper=new listenerMapper(event->events.add(event.data),7,new Key(),key,30000,0);Debug.logs=0;String outcome="OK";try{mapper.valueRead(new Contact(),value);}catch(IndexOutOfBoundsException failure){outcome="THROW";}if(outcome.equals("OK")&&Debug.logs>0)outcome="LOG";StringJoiner values=new StringJoiner(",");for(byte[] event:events)values.add(HexFormat.of().formatHex(event));System.out.println(outcome+"\\t"+(mapper.complete_disabled?1:0)+"\\t"+(mapper.dht.next==null?"":HexFormat.of().formatHex(mapper.dht.next))+"\\t"+events.size()+"\\t"+values);}}}
}
'''
out=Path(a.output);out.parent.mkdir(parents=True,exist_ok=True);out.write_text(head+body+foot);(out.parent/'source-scope.json').write_text(json.dumps({'symbol':'DDBaseImpl.listenerMapper.valueRead normal else block','source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'block_sha256':hashlib.sha256(body.encode()).hexdigest(),'scope':'exact normal-read block with memory event/log/DHT/SHA1 adapters; no stats/network/completion runtime equality claim'},indent=2)+'\n')
