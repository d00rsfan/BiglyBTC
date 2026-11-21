// SPDX-License-Identifier: GPL-2.0-or-later
package com.biglybt.core.tag.impl;
import java.lang.reflect.*;
import java.nio.file.*;
import java.util.*;
import com.biglybt.core.download.*;
import com.biglybt.core.peer.*;
import com.biglybt.core.networkmanager.LimitedRateGroup;
import sun.misc.Unsafe;
public final class OriginalSharedTagRate {
 static final Unsafe U;static{try{Field f=Unsafe.class.getDeclaredField("theUnsafe");f.setAccessible(true);U=(Unsafe)f.get(null);}catch(Exception e){throw new RuntimeException(e);}}
 static int assertions,unexpected;static final Map<String,String> used=new TreeMap<>();static Path original;
 static void check(boolean b,String m){assertions++;if(!b)throw new AssertionError(m);}
 static void set(Object o,String field,Object value)throws Exception{Field f=TagDownloadWithState.class.getDeclaredField(field);f.setAccessible(true);f.set(o,value);}
 interface Call{Object call(String method,Object[] args)throws Exception;}
 @SuppressWarnings("unchecked") static <T>T proxy(Class<T> type,Call call){return (T)Proxy.newProxyInstance(type.getClassLoader(),new Class<?>[]{type},(o,m,a)->{if(m.getDeclaringClass()==Object.class){if(m.getName().equals("hashCode"))return System.identityHashCode(o);if(m.getName().equals("equals"))return o==a[0];return "generated "+type.getSimpleName();}return call.call(m.getName(),a==null?new Object[0]:a);});}
 static Object bad(String method){unexpected++;throw new AssertionError("Unmodelled dependency: "+method);}
 static void pin(Class<?> type)throws Exception{check(Paths.get(type.getProtectionDomain().getCodeSource().getLocation().toURI()).toRealPath().equals(original),"Unpinned "+type);byte[] bytes=Files.readAllBytes(original.resolve(type.getName().replace('.','/')+".class"));byte[] hash=java.security.MessageDigest.getInstance("SHA-256").digest(bytes);StringBuilder s=new StringBuilder();for(byte b:hash)s.append(String.format("%02x",b));used.put(type.getName(),s.toString());}
 static final class RateTag extends TagDownloadWithState{Set<DownloadManager> members=new LinkedHashSet<>();RateTag(){super(null,1,"Generated",true,true,true,false,0);throw new AssertionError("No constructor execution");}@Override public Set<DownloadManager> getTaggedDownloads(){return members;}}
 static final class Peer{
  final Set<Object> up=Collections.newSetFromMap(new IdentityHashMap<>()),down=Collections.newSetFromMap(new IdentityHashMap<>());
  final PEPeer value=proxy(PEPeer.class,(name,args)->{if(name.equals("setUploadDisabled")||name.equals("setDownloadDisabled")){Set<Object> set=name.equals("setUploadDisabled")?up:down;if((Boolean)args[1])set.add(args[0]);else set.remove(args[0]);return null;}return bad(name);});
 }
 static final class Manager{
  final List<Peer> peers=new ArrayList<>();final Map<Object,Object> data=new IdentityHashMap<>();final List<DownloadManagerPeerListener> listeners=new ArrayList<>();final Map<LimitedRateGroup,Boolean> groups=new IdentityHashMap<>();int adds,removes;
  final PEPeerManager pm=proxy(PEPeerManager.class,(name,args)->{if(name.equals("getPeers")){List<PEPeer> result=new ArrayList<>();for(Peer p:peers)result.add(p.value);return result;}return bad(name);});
  final DownloadManager value=proxy(DownloadManager.class,(name,args)->{
   if(name.equals("getUserData"))return data.get(args[0]);if(name.equals("setUserData")){data.put(args[0],args[1]);return null;}if(name.equals("getPeerManager"))return pm;
   if(name.equals("addPeerListener")){DownloadManagerPeerListener l=(DownloadManagerPeerListener)args[0];listeners.add(l);if((Boolean)args[1])for(Peer p:peers)l.peerAdded(p.value);return null;}
   if(name.equals("removePeerListener")){check(listeners.remove(args[0]),"Missing removal listener");return null;}
   if(name.equals("addRateLimiter")){check(!groups.containsKey(args[0]),"Duplicate group attach");groups.put((LimitedRateGroup)args[0],(Boolean)args[1]);adds++;return null;}
   if(name.equals("removeRateLimiter")){check(groups.remove(args[0])!=null,"Missing group detach");removes++;return null;}return bad(name);
  });
  Peer peer(){Peer p=new Peer();peers.add(p);for(DownloadManagerPeerListener l:listeners)l.peerAdded(p.value);return p;}
 }
 static final Method MEMBERSHIP,LIMIT;
 static{try{MEMBERSHIP=TagDownloadWithState.class.getDeclaredMethod("setRateLimit",DownloadManager.class,boolean.class);MEMBERSHIP.setAccessible(true);LIMIT=TagDownloadWithState.class.getDeclaredMethod("setRateLimit",int.class,boolean.class);LIMIT.setAccessible(true);}catch(Exception e){throw new RuntimeException(e);}}
 static RateTag tag()throws Exception{
  RateTag tag=(RateTag)U.allocateInstance(RateTag.class);tag.members=new LinkedHashSet<>();set(tag,"rate_lock",new Object());set(tag,"do_up",true);set(tag,"do_down",true);set(tag,"upload_rate_limit",1234);set(tag,"download_rate_limit",5678);
  int groups=0,listeners=0;Path dir=original.resolve("com/biglybt/core/tag/impl");try(java.util.stream.Stream<Path> paths=Files.list(dir)){for(Path path:(Iterable<Path>)paths.filter(x->x.getFileName().toString().matches("TagDownloadWithState\\$[0-9]+\\.class"))::iterator){String n=path.getFileName().toString();Class<?> type=Class.forName("com.biglybt.core.tag.impl."+n.substring(0,n.length()-6),false,TagDownloadWithState.class.getClassLoader());if(!LimitedRateGroup.class.isAssignableFrom(type)&&!DownloadManagerPeerListener.class.isAssignableFrom(type))continue;Constructor<?> ctor=type.getDeclaredConstructor(TagDownloadWithState.class);ctor.setAccessible(true);Object value=ctor.newInstance(tag);pin(type);if(value instanceof LimitedRateGroup){long rate=((LimitedRateGroup)value).getRateLimitBytesPerSecond();check(rate==1234||rate==5678,"Unknown actual group direction");set(tag,rate==1234?"upload_limiter":"download_limiter",value);groups++;}else{set(tag,"peer_listener",value);listeners++;}}}
  check(groups==2&&listeners==1,"Unexpected actual anonymous callback census");LIMIT.invoke(tag,0,true);LIMIT.invoke(tag,0,false);return tag;
 }
 static void member(RateTag tag,Manager dm,boolean added)throws Exception{if(added)tag.members.add(dm.value);else tag.members.remove(dm.value);MEMBERSHIP.invoke(tag,dm.value,added);}
 public static void main(String[] args)throws Exception{
  original=Paths.get(args[0]).toRealPath();pin(TagDownloadWithState.class);pin(com.biglybt.core.networkmanager.impl.NetworkManagerUtilities.class);RateTag a=tag(),b=tag();Manager publicOwner=new Manager(),overlayOwner=new Manager();Peer p=publicOwner.peer(),i=overlayOwner.peer();
  for(Manager dm:Arrays.asList(publicOwner,overlayOwner)){member(a,dm,true);member(a,dm,true);check(dm.adds==2&&dm.listeners.size()==1,"Duplicate membership replayed effects");}
  check(publicOwner.groups.keySet().equals(overlayOwner.groups.keySet()),"Different actual group identities across owners");
  for(Manager dm:Arrays.asList(publicOwner,overlayOwner))member(b,dm,true);
  LIMIT.invoke(a,1,true);LIMIT.invoke(a,-1,false);LIMIT.invoke(b,-1,true);LIMIT.invoke(b,4096,false);
  for(Manager dm:Arrays.asList(publicOwner,overlayOwner)){check(dm.groups.size()==4,"Missing independent tag groups");for(Map.Entry<LimitedRateGroup,Boolean> entry:dm.groups.entrySet()){long n=entry.getKey().getRateLimitBytesPerSecond();check(n==0||n==(entry.getValue()?1:4096),"Wrong actual current shared rate");check(com.biglybt.core.networkmanager.impl.NetworkManagerUtilities.getGroupRateLimit(entry.getKey())==(n==0?(1L<<40):n),"Actual disabled group must retain unlimited protocol byte quota");}}
  for(Peer peer:Arrays.asList(p,i)){check(peer.up.size()==1&&peer.down.size()==1,"Independent disabled directions lost");}
  Peer late=overlayOwner.peer();check(late.up.size()==1&&late.down.size()==1,"Late peer missed independent prohibitions");
  for(Manager dm:Arrays.asList(publicOwner,overlayOwner)){member(a,dm,false);member(a,dm,false);check(dm.removes==2&&dm.listeners.size()==1&&dm.groups.size()==2,"Detach/noop membership mismatch");}
  for(Peer peer:Arrays.asList(p,i,late))check(peer.up.size()==1&&peer.down.isEmpty(),"Detaching one tag cleared another prohibition");
  member(b,publicOwner,false);check(p.up.isEmpty()&&i.up.size()==1,"Owner-specific detach leaked");LIMIT.invoke(b,16,true);for(Peer peer:Arrays.asList(p,i,late))check(peer.up.isEmpty()&&peer.down.isEmpty(),"Positive transition retained disabled state");
  member(b,overlayOwner,false);check(publicOwner.groups.isEmpty()&&overlayOwner.groups.isEmpty(),"Final detach retained rate groups");check(unexpected==0,"Unknown dependency calls");check(!com.biglybt.core.CoreFactory.isCoreAvailable(),"Core unexpectedly started");
  for(Map.Entry<String,String> e:used.entrySet())System.out.println("PIN\t"+e.getKey()+"\t"+e.getValue());System.out.println("PASS_ORIGINAL_SHARED_TAG_OWNER "+assertions+" assertions;2 owner facades;2 tags;actual shared groups/callbacks;no network getter/Core");com.biglybt.core.security.SESecurityManager.exitVM(0);
 }
}
