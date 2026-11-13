# Exact original selected notice blocks and full getFlagOrigin method.
from pathlib import Path
import argparse,hashlib,json,re
p=argparse.ArgumentParser();p.add_argument('--rating',required=True);p.add_argument('--buddy',required=True);p.add_argument('--output',required=True);a=p.parse_args()
r=Path(a.rating).read_text();b=Path(a.buddy).read_text();scope=r.index('writeRating(');begin=r.index('String msg = "Rating: "',scope);end=r.index('final String f_msg = msg;',begin);text=r[begin:end]
begin=r.index('if ( messages.size() > 50 )',end);end=r.index('do_write.run();',begin)+len('do_write.run();');decision=r[begin:end]
begin=re.search(r'public int\s+getFlagOrigin\(\)',b).start();mask=b;brace=mask.index('{',begin);depth=0
for end in range(brace,len(mask)):
 if mask[end]=='{':depth+=1
 elif mask[end]=='}':
  depth-=1
  if depth==0:break
origin=b[begin:end+1]
assert 'FLAGS_MSG_ORIGIN_USER' in origin and 'getPayload()' in origin
head=r'''// Synthetic in-memory collaborators; no original network or UI runtime.
import java.io.*;import java.util.*;
public final class RatingChatOracle {
 static String string(String hex){char[] chars=new char[hex.length()/4];for(int i=0;i<chars.length;i++)chars[i]=(char)Integer.parseInt(hex.substring(i*4,i*4+4),16);return new String(chars);}
 static String hex(String s){StringBuilder b=new StringBuilder();for(char c:s.toCharArray())b.append(String.format(Locale.ROOT,"%04x",(int)c));return b.toString();}
 static class Data{int score;String comment;int getScore(){return score;}String getComment(){return comment;}}
 static class ChatMessage{String message;boolean own;ChatMessage getParticipant(){return this;}boolean isMe(){return own;}String getMessage(){return message;}}
 static String text(Data data){
'''
mid=r'''
 return msg;}
 static boolean sent;static void decide(List<ChatMessage> messages,String f_msg){Runnable do_write=()->sent=true;
'''
originhead=r'''
 }
 static final String FLAGS_MSG_ORIGIN_KEY="o";static final int FLAGS_MSG_ORIGIN_USER=0,FLAGS_MSG_ORIGIN_RATINGS=1;
 Map payload;String message;Map getPayload(){return payload;}String getMessage(){return message;}
'''
foot=r'''
 public static void main(String[] args)throws Exception{try(BufferedReader in=new BufferedReader(new InputStreamReader(System.in,java.nio.charset.StandardCharsets.US_ASCII))){for(String line;(line=in.readLine())!=null;){String[] bits=line.split("\t",-1);if(bits[0].equals("O")){RatingChatOracle owner=new RatingChatOracle();owner.message=string(bits[1]);if(!bits[2].equals("-")){Map flags=new HashMap();flags.put("o",Long.parseLong(bits[2]));owner.payload=new HashMap();owner.payload.put("f",flags);}System.out.println(owner.getFlagOrigin());continue;}Data data=new Data();data.score=Integer.parseInt(bits[1]);data.comment=string(bits[2]);String text=text(data);List<ChatMessage> rows=new ArrayList<>();if(!bits[4].isEmpty()){for(String row:bits[4].split(",",-1)){ChatMessage msg=new ChatMessage();msg.own=row.charAt(0)=='1';msg.message=string(row.substring(1));rows.add(msg);}}sent=false;if(bits[3].equals("0"))sent=true;else decide(rows,text);System.out.println(hex(text)+"\t"+(sent?1:0));}}}
}
'''
out=Path(a.output);out.parent.mkdir(parents=True,exist_ok=True);out.write_text(head+text+mid+decision+originhead+origin+foot)
(out.parent/'source-scope.json').write_text(json.dumps({'scope':'exact selected original notice construction/history predicate and full original getFlagOrigin with memory collaborators; no timer/network/UI runtime claim','rating_source_sha256':hashlib.sha256(r.encode()).hexdigest(),'buddy_source_sha256':hashlib.sha256(b.encode()).hexdigest(),'notice_sha256':hashlib.sha256(text.encode()).hexdigest(),'decision_sha256':hashlib.sha256(decision.encode()).hexdigest(),'origin_sha256':hashlib.sha256(origin.encode()).hexdigest()},indent=2)+'\n')
