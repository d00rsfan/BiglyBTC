from pathlib import Path
import argparse,hashlib,json,re
p=argparse.ArgumentParser();p.add_argument('--view',required=True);p.add_argument('--beta',required=True);p.add_argument('--output',required=True);a=p.parse_args();view=Path(a.view).read_text();beta=Path(a.beta).read_text()
def block(source,pattern,start=0):
 m=re.search(pattern,source[start:]);assert m,pattern;begin=start+m.start();brace=source.index('{',begin);depth=0
 for end in range(brace,len(source)):
  if source[end]=='{':depth+=1
  elif source[end]=='}':
   depth-=1
   if depth==0:return source[begin:end+1]
 raise ValueError('Unclosed original method')
perform=block(view,r'public\s+void\s+perform\(\s*TimerEvent\s+event\s*\)',view.index('"BPPM"'))
sound=block(view,r'protected\s+void\s+playSound\(\s*\)')
nicks=block(beta,r'public\s+int\[\]\s+getNickLocations\(\s*\)')
head="""import java.io.*;import java.util.*;import java.security.*;
public final class ChatNotificationOracle {
 static class Fence extends SecurityManager{public void checkPermission(Permission p){}public void checkConnect(String h,int p){throw new SecurityException("No reference network");}public void checkListen(int p){throw new SecurityException("No reference listener");}}
 static int played;static class GeneralUtils{static void playSound(String file){played++;}}
 static class TimerEvent{}static class TimerEventPeriodic{boolean cancelled;void cancel(){cancelled=true;}}
 static class Control{boolean isDisposed(){return false;}}
 static class Image{String name;Image(String value){name=value;}}
 static class Utils{static void setTT(Object status,String value){}}
 static class Plugin{Beta beta=new Beta();Beta getBeta(){return beta;}}
 static class Beta{boolean sound,flash;boolean getSoundEnabled(){return sound;}boolean getFlashEnabled(){return flash;}String getSoundFile(){return "generated";}}
 static class Chat{String nick;String getNickname(boolean value){return nick;}}
 static class Participant{Chat chat=new Chat();Chat getChat(){return chat;}}
 static class Message{int[] nick_locations;Participant participant=new Participant();String text;String getMessage(){return text;}
"""
mid="""
 }
 static class ChatInstance{String name;boolean disabled,mine,destroyed;boolean isDestroyed(){return destroyed;}boolean hasUnseenMessageWithNick(){return mine;}boolean getDisableNewMsgIndications(){return disabled;}String getShortName(){return name;}}
 Map<String,Object[]> pending_msg_map=new LinkedHashMap<>();Map<String,ChatInstance> instances=new HashMap<>();TimerEventPeriodic pending_msg_event;Plugin plugin=new Plugin();Object beta_status=new Object();long generation;Worker worker;
 Image bs_chat_gray_text=new Image("dim"),bs_chat_red=new Image("mention"),bs_chat_green=new Image("message");String icon="idle";
 void updateIdleTT(boolean known){icon="idle";}void buildMenu(Set<ChatInstance> value,boolean pending){}List<ChatInstance> sortChats(Set<ChatInstance> value){return new ArrayList<>(value);}void setBetaStatus(Image value){icon=value.name;}
"""
worker="""
 class Worker{private int tick_count=0;private Set<ChatInstance> prev_instances=new HashSet<>();
"""
foot=r"""
 }
 static String text(String hex){char[] chars=new char[hex.length()/4];for(int n=0;n<chars.length;n++)chars[n]=(char)Integer.parseInt(hex.substring(n*4,n*4+4),16);return new String(chars);}
 String step(String[] fields){long next=Long.parseLong(fields[1]);if(next<generation)return icon+" 0";if(next!=generation){generation=next;pending_msg_map.clear();instances.clear();pending_msg_event=null;worker=null;icon="idle";}
  plugin.beta.sound=fields[2].equals("1");plugin.beta.flash=fields[3].equals("1");pending_msg_map.clear();
  if(next!=0&&!fields[4].isEmpty()){for(String row:fields[4].split(",")){String[] r=row.split(":");String key=text(r[0]),identity=key+"/"+r[1];ChatInstance chat=instances.get(identity);if(chat==null){chat=new ChatInstance();chat.name=key;instances.put(identity,chat);}chat.disabled=r[3].equals("1");chat.mine=r[4].equals("1");chat.destroyed=r[5].equals("0");int count=Integer.parseInt(r[2]);if(count>0){HashSet<Control> controls=new HashSet<>();controls.add(null);pending_msg_map.put(key,new Object[]{count,controls,chat});}}}
  if(!pending_msg_map.isEmpty()&&pending_msg_event==null){pending_msg_event=new TimerEventPeriodic();worker=new Worker();}
  played=0;if(worker!=null){worker.perform(new TimerEvent());if(pending_msg_event==null){worker=null;}}
  return icon+" "+(played>0?1:0);
 }
 public static void main(String[] args)throws Exception{System.setProperty("user.home",args[0]);System.setProperty("azureus.config.path",args[0]+"/profile");System.setSecurityManager(new Fence());ChatNotificationOracle value=new ChatNotificationOracle();try(BufferedReader in=new BufferedReader(new InputStreamReader(System.in,java.nio.charset.StandardCharsets.US_ASCII))){for(String line;(line=in.readLine())!=null;){String[] f=line.split("\t",-1);if(f[0].equals("N")){Message message=new Message();message.text=text(f[1]);message.participant.chat.nick=text(f[2]);int[] hits=message.getNickLocations();StringJoiner result=new StringJoiner(",");for(int hit:hits)result.add(Integer.toString(hit));System.out.println(result);}else{System.out.println(value.step(f));}}}}
}
"""
out=Path(a.output);out.parent.mkdir(parents=True,exist_ok=True);out.write_text(head+nicks+mid+sound+worker+perform+foot)
(out.parent/'source-scope.json').write_text(json.dumps({'view_sha256':hashlib.sha256(view.encode()).hexdigest(),'beta_sha256':hashlib.sha256(beta.encode()).hexdigest(),'selected_methods_sha256':[hashlib.sha256(m.encode()).hexdigest() for m in (perform,sound,nicks)],'scope':'exact original pending-timer performer, sound switch and nickname-location method; generated stable room/control/config collaborators, recorded image/audio effects; no SWT lifecycle/network/audio output; selected stable Unicode characters only'},indent=2)+'\n')
