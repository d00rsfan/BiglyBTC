from pathlib import Path
import argparse,re,json,hashlib
p=argparse.ArgumentParser();p.add_argument('--view',required=True);p.add_argument('--output',required=True);a=p.parse_args();source=Path(a.view).read_text()
def method(name):
 start=re.search(r'private\s+(?:static\s+)?(?:void|Pattern|String)\s+'+name+r'\(',source).start();first=source.index('{',start);depth=0
 for end in range(first,len(source)):
  if source[end]=='{':depth+=1
  elif source[end]=='}':
   depth-=1
   if depth==0:return source[start:end+1]
 raise ValueError('Original method not closed')
methods=[method(name) for name in ['getEmphasisPattern','expandEmphasis','search']]
head="""import java.io.*;import java.util.*;import java.util.regex.*;import java.security.*;import com.biglybt.core.util.UrlUtils;import com.biglybt.core.util.RegExUtil;
public final class ChatTranscriptOracle {
 static class Fence extends SecurityManager{public void checkPermission(Permission p){}public void checkConnect(String h,int p){throw new SecurityException("No reference network");}public void checkListen(int p){throw new SecurityException("No reference listener");}public void checkAccept(String h,int p){throw new SecurityException("No reference accept");}}
 static class Log{String text;int first,last;String getText(){return text;}void setSelection(int n){first=last=n;}void setSelection(int a,int b){first=a;last=b;}}
 Log log=new Log();String current_search="";int current_search_index;
"""
foot="""
 static String text(String hex){char[] chars=new char[hex.length()/4];for(int n=0;n<chars.length;n++)chars[n]=(char)Integer.parseInt(hex.substring(n*4,n*4+4),16);return new String(chars);}
 static String hex(String text){StringBuilder result=new StringBuilder();for(char c:text.toCharArray())result.append(String.format(java.util.Locale.ROOT,"%04x",(int)c));return result.toString();}
 public static void main(String[] args)throws Exception{System.setProperty("user.home",args[0]);System.setProperty("azureus.config.path",args[0]+"/profile");System.setSecurityManager(new Fence());Locale.setDefault(Locale.ENGLISH);ChatTranscriptOracle value=new ChatTranscriptOracle();try(BufferedReader in=new BufferedReader(new InputStreamReader(System.in,java.nio.charset.StandardCharsets.US_ASCII))){for(String line;(line=in.readLine())!=null;){String[] fields=line.split("\\t",-1);if(fields[0].equals("W")){StringBuilder out=new StringBuilder();for(int c=0;c<=65535;c++)out.append(Character.isWhitespace((char)c)?'1':'0');System.out.println(out);}else if(fields[0].equals("E")){System.out.println(hex(expandEmphasis(text(fields[1]))));}else if(fields[0].equals("U")){try{System.out.println("1:"+hex(UrlUtils.decode(text(fields[1]))));}catch(Exception e){System.out.println("0:");}}else if(fields[0].equals("P")){System.out.println(hex(UrlUtils.encode(text(fields[1]))));}else{value.log.text=text(fields[1]);value.search(text(fields[2]));System.out.println(value.log.first+" "+value.log.last+" "+value.current_search_index+" "+hex(value.current_search));}}}}
}
"""
out=Path(a.output);out.parent.mkdir(parents=True,exist_ok=True);out.write_text(head+'\n'.join(methods)+foot);(out.parent/'source-scope.json').write_text(json.dumps({'view_sha256':hashlib.sha256(source.encode()).hexdigest(),'methods_sha256':[hashlib.sha256(value.encode()).hexdigest() for value in methods],'scope':'exact original emphasis and search methods; real core UrlUtils/RegExUtil and JDK English locale/Character; generated log/selection and fenced temporary profile; no SWT window/network lifecycle'},indent=2)+'\n')
