from pathlib import Path
import argparse,re,json,hashlib
p=argparse.ArgumentParser();p.add_argument('--buddy',required=True);p.add_argument('--configuration',required=True);p.add_argument('--output',required=True);a=p.parse_args();buddy=Path(a.buddy).read_text();configuration=Path(a.configuration).read_text()
def block(text,start):
 at=text.index('{',start);depth=0
 for end in range(at,len(text)):
  if text[end]=='{':depth+=1
  elif text[end]=='}':
   depth-=1
   if depth==0:return text[start:end+1]
 raise ValueError('Unclosed original method')
def method(pattern,text=buddy):return block(text,re.search(pattern,text).start())
key=method('private\\s+String\\s+getPropsKey\\(\\)');setter=method('private\\s+void\\s+setProperty\\(\\s*String\\s+name,\\s*boolean\\s+value\\s*\\)');ignored=method('public\\s+void\\s+setIgnored\\(\\s*boolean\\s+b\\s*\\)');pinned=method('public\\s+void\\s+setPinned\\(\\s*boolean\\s+b\\s*\\)')
start=buddy.index('Map props',buddy.index('nickname = pkToString( pk );'));stop=buddy.index('chat.registerNick',start);constructor=buddy[start:stop]
globalBoolean=method('public boolean getBooleanParameter\\(String parameter, boolean defaultValue\\)',configuration);globalInt=method('public int getIntParameter\\(String parameter, int defaultValue\\)',configuration)
head='''import java.io.*;import java.util.*;import java.security.*;import com.biglybt.core.util.*;import com.biglybt.util.MapUtils;
public final class ChatPreferencesOracle {
 static class Fence extends SecurityManager{public void checkPermission(Permission p){}public void checkConnect(String h,int p){throw new SecurityException("No test network");}public void checkListen(int p){throw new SecurityException("No test listener");}public void checkAccept(String h,int p){throw new SecurityException("No test accept");}}
 static class Globals{Long getLongParameterRaw(String parameter){Object value=COConfigurationManager.cfg.get(parameter);return value instanceof Long?(Long)value:null;}
'''
mid='''
 }
 static class COConfigurationManager{static Map<String,Object> cfg;static Map getMapParameter(String key,Map def){Object value=cfg.get(key);return value instanceof Map?(Map)value:def;}static boolean getBooleanParameter(String key,boolean def){return new Globals().getBooleanParameter(key,def);}static boolean removeParameter(String key){return cfg.remove(key)!=null;}static void setParameter(String key,Map value){cfg.put(key,value);}static void setDirty(){}}
 static class Chat{Object chat_lock=new Object();}static class ChatMessage{void setIgnored(boolean value){}}
 Chat chat=new Chat();byte[] pk;boolean is_pinned,is_ignored,is_spammer;List profile_data_cache;List<ChatMessage> participant_messages=new ArrayList<>();
 ChatPreferencesOracle(byte[] key){pk=key;
'''
foot='''
 public static void main(String[]args)throws Exception{System.setProperty("user.home",args[0]);System.setProperty("azureus.config.path",args[0]+"/profile");System.setSecurityManager(new Fence());try(BufferedReader in=new BufferedReader(new InputStreamReader(System.in,java.nio.charset.StandardCharsets.US_ASCII))){for(String line;(line=in.readLine())!=null;){String[] f=line.split("\\t",-1);COConfigurationManager.cfg=BDecoder.decode(HexFormat.of().parseHex(f[2]));ChatPreferencesOracle value=new ChatPreferencesOracle(HexFormat.of().parseHex(f[1]));if(f[0].equals("I"))value.setIgnored(f[3].equals("1"));else if(f[0].equals("P"))value.setPinned(f[3].equals("1"));System.out.println(value.getPropsKey()+"\\t"+(COConfigurationManager.getBooleanParameter("azbuddy.dchat.ui.hide.ratings",false)?1:0)+" "+(COConfigurationManager.getBooleanParameter("azbuddy.dchat.ui.hide.search_subs",false)?1:0)+" "+(value.is_ignored?1:0)+" "+(value.is_pinned?1:0)+"\\t"+HexFormat.of().formatHex(BEncoder.encode(COConfigurationManager.cfg)));}}}
}
'''
out=Path(a.output);out.parent.mkdir(parents=True,exist_ok=True);out.write_text(head+globalBoolean+'\n'+globalInt+mid+constructor+'\n}\n'+key+'\n'+setter+'\n'+ignored+'\n'+pinned+foot);scope={'buddy_sha256':hashlib.sha256(buddy.encode()).hexdigest(),'configuration_sha256':hashlib.sha256(configuration.encode()).hexdigest(),'blocks_sha256':{name:hashlib.sha256(value.encode()).hexdigest() for name,value in [('getPropsKey',key),('booleanSetter',setter),('setIgnored',ignored),('setPinned',pinned),('participantConstructorProperties',constructor),('globalBoolean',globalBoolean),('globalInt',globalInt)]},'scope':'exact selected original methods/constructor properties; real ByteFormatter/MapUtils/BEncoder/BDecoder; in-memory config and empty participant-message collaborators; no real profile/UI/network lifecycle'};(out.parent/'source-scope.json').write_text(json.dumps(scope,indent=2)+'\n')
