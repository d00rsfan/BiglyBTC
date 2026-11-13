from pathlib import Path
import argparse,re,json,hashlib
p=argparse.ArgumentParser();p.add_argument('--view',required=True);p.add_argument('--output',required=True);a=p.parse_args();source=Path(a.view).read_text()
def method(name):
 start=re.search(r'(?:protected|private)\s+(?:static\s+)?(?:void|Pattern)\s+'+name+r'\(',source).start();first=source.index('{',start);depth=0
 for end in range(first,len(source)):
  if source[end]=='{':depth+=1
  elif source[end]=='}':
   depth-=1
   if depth==0:return source[start:end+1]
 raise ValueError('Original method not closed')
methods=[method(name) for name in ['sendMessage','getEmphasisPattern','addNickString']]
head="""import java.io.*;import java.util.*;import java.util.regex.*;
public final class ChatComposerOracle {
 static final int MAX_MSG_CHUNK_ENABLE=500,MAX_MSG_CHUNK_LENGTH=400;
 static class RegExUtil{static Pattern getCachedPattern(String key,String value){return Pattern.compile(value);}}
 static class Chat {List<String> sent=new ArrayList<>();void sendMessage(String value,Map options){sent.add(value);}}
 static class ChatParticipant {String name;ChatParticipant(String value){name=value;}String getName(boolean nick){return name;}}
 static class Input {String text;int caret;String getText(){return text;}int getCaretPosition(){return caret;}void setSelection(int first,int last){caret=last;}void insert(String value){text=text.substring(0,caret)+value+text.substring(caret);caret+=value.length();}}
 Chat chat=new Chat();Input input_area=new Input();
"""
foot="""
 static String text(String hex){char[] chars=new char[hex.length()/4];for(int n=0;n<chars.length;n++)chars[n]=(char)Integer.parseInt(hex.substring(n*4,n*4+4),16);return new String(chars);}
 static String hex(String text){StringBuilder result=new StringBuilder();for(char c:text.toCharArray())result.append(String.format(java.util.Locale.ROOT,"%04x",(int)c));return result.toString();}
 public static void main(String[] args)throws Exception{try(BufferedReader in=new BufferedReader(new InputStreamReader(System.in,java.nio.charset.StandardCharsets.US_ASCII))){for(String line;(line=in.readLine())!=null;){String[] fields=line.split("\\t",-1);ChatComposerOracle value=new ChatComposerOracle();if(fields[0].equals("M")){value.input_area.text=text(fields[2]);value.input_area.caret=Integer.parseInt(fields[3]);value.addNickString(new ChatParticipant(text(fields[1])));System.out.println(hex(value.input_area.text)+" "+value.input_area.caret);}else{value.sendMessage(text(fields[1]),fields[2].equals("1"));StringJoiner result=new StringJoiner(",");for(String message:value.chat.sent)result.add(hex(message));System.out.println(value.chat.sent.size()+":"+result);}}}}
}
"""
out=Path(a.output);out.parent.mkdir(parents=True,exist_ok=True);out.write_text(head+'\n'.join(methods)+foot);(out.parent/'source-scope.json').write_text(json.dumps({'view_sha256':hashlib.sha256(source.encode()).hexdigest(),'methods_sha256':[hashlib.sha256(value.encode()).hexdigest() for value in methods],'scope':'exact original UI formatter/emphasis/nickname insertion; real JDK Pattern/Matcher and UTF16 strings; in-memory pattern cache/input/output collectors; no SWT, signing, command or network lifecycle'},indent=2)+'\n')
