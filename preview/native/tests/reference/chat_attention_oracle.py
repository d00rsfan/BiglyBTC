# Exact selected original message methods, attention branch and SWT filter.
from pathlib import Path
import argparse,re,json,hashlib
p=argparse.ArgumentParser();p.add_argument('--buddy',required=True);p.add_argument('--view',required=True);p.add_argument('--output',required=True);a=p.parse_args();core=Path(a.buddy).read_text();view=Path(a.view).read_text()
def end(text,start):
 first=text.index('{',start);depth=0
 for i in range(first,len(text)):
  if text[i]=='{':depth+=1
  elif text[i]=='}':
   depth-=1
   if depth==0:return i+1
 raise ValueError('Original block not closed')
def method(name):
 start=re.search(r'(?:public|private)\s+(?:boolean|int)\s+'+name+r'\(\)',core).start();return core[start:end(core,start)]
methods=[method('getFlagFlashOverride'),method('getFlagOrigin'),method('getFlagType')]
anchor=core.index('if ( log_messages )',core.index('messages_not_mine_count--;'));start=re.search(r'if\s*\(\s*participant\.isMe\(\)\s*\)\s*\{',core[anchor:]).start()+anchor;first=end(core,start);second=re.match(r'\s*else\s*\{',core[first:]);assert second;attention=core[start:end(core,first+second.start())]
start=view.index('if ( !message.isIgnored() && original_msg.length() > 0 )');stop=view.index('long time = message.getTimeStamp();',start);visible=view[start:stop]
head='''import java.io.*;import java.util.*;import java.security.*;import java.net.InetSocketAddress;
public final class ChatAttentionOracle {
 static class Fence extends SecurityManager{public void checkPermission(Permission p){}public void checkConnect(String h,int p){throw new SecurityException("No test network");}public void checkListen(int p){throw new SecurityException("No test listener");}public void checkAccept(String h,int p){throw new SecurityException("No test accept");}}
 static final String FLAGS_MSG_FLASH_OVERRIDE="f",FLAGS_MSG_ORIGIN_KEY="o",FLAGS_MSG_TYPE_KEY="t";static final int FLAGS_MSG_ORIGIN_USER=0,FLAGS_MSG_ORIGIN_RATINGS=1,FLAGS_MSG_ORIGIN_SUBS=3,FLAGS_MSG_TYPE_NORMAL=0;
 static class BuddyPluginBeta{static final int FLAGS_MSG_ORIGIN_RATINGS=1,FLAGS_MSG_ORIGIN_SUBS=3;}
 InetSocketAddress my_address;InetSocketAddress getAddress(){return null;}
 Map<String,Object> payload;String text;boolean local,ignored,hideRatings,hideSubs;ChatAttentionOracle last_message_requiring_attention;int messages_not_mine_count;
 Map<String,Object> getPayload(){return payload;}String getMessage(){return text;}boolean isMe(){return local;}boolean isIgnored(){return ignored;}boolean getHideRatings(){return hideRatings;}boolean getHideSearchSubs(){return hideSubs;}
 boolean attention(){ChatAttentionOracle participant=this,msg=this;
'''
mid='''
 return last_message_requiring_attention!=null;}
 boolean visible(){ChatAttentionOracle message=this;String original_msg=text;boolean ignore_ratings=hideRatings,ignore_search_subs=hideSubs;for(int once=0;once<1;once++){
'''
foot='''
 return true;} }return false;}
 static String text(String hex){char[] chars=new char[hex.length()/4];for(int n=0;n<chars.length;n++)chars[n]=(char)Integer.parseInt(hex.substring(n*4,n*4+4),16);return new String(chars);}
 public static void main(String[]args)throws Exception{System.setSecurityManager(new Fence());try(BufferedReader in=new BufferedReader(new InputStreamReader(System.in,java.nio.charset.StandardCharsets.US_ASCII))){for(String line;(line=in.readLine())!=null;){String[] f=line.split("\\t",-1);ChatAttentionOracle value=new ChatAttentionOracle();value.local=f[0].equals("1");value.ignored=f[1].equals("1");value.hideRatings=f[2].equals("1");value.hideSubs=f[3].equals("1");Map<String,Object> flags=new HashMap<>();for(int n=0;n<3;n++){if(!f[4+n].equals("-"))flags.put(n==0?"f":n==1?"t":"o",Long.parseLong(f[4+n]));}if(!flags.isEmpty()){value.payload=new HashMap<>();value.payload.put("f",flags);}value.text=text(f[7]);boolean attention=value.attention();System.out.println((value.getFlagFlashOverride()?1:0)+" "+value.getFlagType()+" "+value.getFlagOrigin()+" "+(attention?1:0)+" "+(value.visible()?1:0)+" "+value.messages_not_mine_count);}}}
}
'''
head=head[:head.index(" boolean attention(){")]
out=Path(a.output);out.parent.mkdir(parents=True,exist_ok=True);out.write_text(head+'\n'.join(methods)+'\n boolean attention(){ChatAttentionOracle participant=this,msg=this;\n'+attention+mid+visible+foot);scope={'buddy_sha256':hashlib.sha256(core.encode()).hexdigest(),'view_sha256':hashlib.sha256(view.encode()).hexdigest(),'method_sha256':[hashlib.sha256(m.encode()).hexdigest() for m in methods],'attention_sha256':hashlib.sha256(attention.encode()).hexdigest(),'visible_sha256':hashlib.sha256(visible.encode()).hexdigest(),'scope':'exact selected original message methods/Core attention branches/SWT filter prefix; supplied in-memory payload/participant/preferences; no original UI/network/profile lifecycle claim'};(out.parent/'source-scope.json').write_text(json.dumps(scope,indent=2)+'\n')
