from pathlib import Path
import argparse,re,json,hashlib
p=argparse.ArgumentParser();p.add_argument('--source',required=True);p.add_argument('--output',required=True);a=p.parse_args();source=Path(a.source);text=source.read_text();begin=re.search(r'public int\[\]\s+getMessageCounts\(\)',text).start();brace=text.index('{',begin);depth=0
for end in range(brace,len(text)):
 if text[end]=='{':depth+=1
 elif text[end]=='}':
  depth-=1
  if depth==0:break
body=text[begin:end+1]
head='''// Exact original status method; memory-only clock and zero outgoing count.
import java.io.*;import java.util.*;
public final class IncomingSyncOracle {
 static class SystemTime{static long now;static long getMonotonousTime(){return now;}}
 final Object message_lock=new Object();final List messages=new ArrayList();
 int[] msg_count_cache;long msg_count_cache_time;int consec_no_more_to_come,last_more_to_come;
 long first_sync_attempt_time=-1,last_successful_sync_time=-1;
 int getUndeliveredMessageCount(){return 0;}
'''
foot='''
 public static void main(String[] args)throws Exception{IncomingSyncOracle owner=new IncomingSyncOracle();try(BufferedReader input=new BufferedReader(new InputStreamReader(System.in,java.nio.charset.StandardCharsets.US_ASCII))){for(String line;(line=input.readLine())!=null;){String[] bits=line.split(" ");switch(bits[0]){case "Z":owner=new IncomingSyncOracle();System.out.println("OK");break;case "A":if(owner.first_sync_attempt_time==-1)owner.first_sync_attempt_time=Long.parseLong(bits[1]);System.out.println("OK");break;case "R":owner.last_successful_sync_time=Long.parseLong(bits[1]);owner.last_more_to_come=Integer.parseInt(bits[2]);if(owner.last_more_to_come>0)owner.consec_no_more_to_come=0;else owner.consec_no_more_to_come++;System.out.println("OK");break;case "S":SystemTime.now=Long.parseLong(bits[1]);System.out.println(owner.getMessageCounts()[2]);break;default:throw new IOException("Generated status command");}}}}
}
'''
out=Path(a.output);out.parent.mkdir(parents=True,exist_ok=True);out.write_text(head+body+foot);(out.parent/'source-scope.json').write_text(json.dumps({'symbol':'MsgSyncHandler.getMessageCounts','source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'method_sha256':hashlib.sha256(body.encode()).hexdigest(),'scope':'exact original status method with memory clock/messages/outgoing adapters; input state assignments mirror inspected event sites; no original transport scheduler runtime claim'},indent=2)+'\n')
