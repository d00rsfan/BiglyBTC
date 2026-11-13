from pathlib import Path
import argparse,json,hashlib
p=argparse.ArgumentParser();p.add_argument('--handler',required=True);p.add_argument('--output',required=True);a=p.parse_args();source=Path(a.handler).read_text();start=source.index('int now_secs = (int)( SystemTime.getMonotonousTime()/1000 );');end=source.index('last_message_times[last_message_times_pos++%last_message_times.length] = now_secs;',start)+len('last_message_times[last_message_times_pos++%last_message_times.length] = now_secs;');block=source[start:end]
head="""import java.io.*;
public final class ChatSendPaceOracle {
 static final int LAST_MESSAGE_LIMIT=30,LAST_MESSAGE_WINDOW=60;
 static class SystemTime {static long now;static long getMonotonousTime(){return now;}}
 int[] last_message_times=new int[LAST_MESSAGE_LIMIT];int last_message_times_pos,last_flood_warning;boolean managing_ro;void reportErrorRaw(String value){}
 int reserve(long now){SystemTime.now=now;
"""
foot="""
 return delay_millis;}
 public static void main(String[]args)throws Exception{ChatSendPaceOracle value=new ChatSendPaceOracle();try(BufferedReader in=new BufferedReader(new InputStreamReader(System.in))){for(String line;(line=in.readLine())!=null;){System.out.println(value.reserve(Long.parseLong(line)));}}}
}
"""
out=Path(a.output);out.parent.mkdir(parents=True,exist_ok=True);out.write_text(head+block+foot);(out.parent/'pace-source-scope.json').write_text(json.dumps({'handler_sha256':hashlib.sha256(source.encode()).hexdigest(),'block_sha256':hashlib.sha256(block.encode()).hexdigest(),'scope':'exact original per-handler pacing reservation/delay; supplied monotonic clock and local warning collector, no sleep/network/plugin lifecycle'},indent=2)+'\n')
