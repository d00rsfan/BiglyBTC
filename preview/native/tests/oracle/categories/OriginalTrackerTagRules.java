// SPDX-License-Identifier: GPL-2.0-or-later
package com.biglybt.core.tag.impl;
import com.biglybt.core.Core;
import com.biglybt.core.global.GlobalManager;
import com.biglybt.core.download.DownloadManager;
import com.biglybt.core.tag.*;
import com.biglybt.core.tag.TagFeatureProperties.TagProperty;
import com.biglybt.core.torrent.*;
import com.biglybt.core.util.BEncoder;
import java.lang.reflect.*;
import java.net.*;
import java.nio.file.*;
import java.util.*;
import java.util.concurrent.atomic.AtomicBoolean;
import sun.misc.Unsafe;
/** Actual tracker getter, property/startup/new-DM and sync bodies, inert proxy boundaries. */
public final class OriginalTrackerTagRules {
 static final Unsafe U;static{try{Field f=Unsafe.class.getDeclaredField("theUnsafe");f.setAccessible(true);U=(Unsafe)f.get(null);}catch(Exception e){throw new RuntimeException(e);}}
 static void set(Object o,Class<?> c,String name,Object value)throws Exception{Field f=c.getDeclaredField(name);f.setAccessible(true);f.set(o,value);}
 static Object proxy(Class<?> c,InvocationHandler h){return java.lang.reflect.Proxy.newProxyInstance(c.getClassLoader(),new Class[]{c},h);}
 static Object simple(Object p,Method m,Object[] a){switch(m.getName()){case "hashCode":return System.identityHashCode(p);case "equals":return p==a[0];case "toString":return "generated-boundary";}throw new AssertionError("Unexpected boundary: "+m.getName());}
 static URL url(String value)throws Exception{if(value.startsWith("udp:"))return new URL(null,value,new com.biglybt.core.util.protocol.udp.Handler());if(value.startsWith("wss:"))return new URL(null,value,new com.biglybt.core.util.protocol.wss.Handler());return new URL(value);}
 static TOTorrent torrent(List<URL> urls){TOTorrentAnnounceURLSet tier=(TOTorrentAnnounceURLSet)proxy(TOTorrentAnnounceURLSet.class,(p,m,a)->m.getName().equals("getAnnounceURLs")?urls.toArray(new URL[0]):simple(p,m,a));TOTorrentAnnounceURLGroup group=(TOTorrentAnnounceURLGroup)proxy(TOTorrentAnnounceURLGroup.class,(p,m,a)->m.getName().equals("getAnnounceURLSets")?new TOTorrentAnnounceURLSet[]{tier}:simple(p,m,a));return (TOTorrent)proxy(TOTorrent.class,(p,m,a)->m.getName().equals("getAnnounceURL")?(urls.isEmpty()?null:urls.get(0)):m.getName().equals("getAnnounceURLGroup")?group:simple(p,m,a));}
 static DownloadManager download(int id,List<URL> urls){TOTorrent t=torrent(urls);return (DownloadManager)proxy(DownloadManager.class,(p,m,a)->m.getName().equals("getTorrent")?t:m.getName().equals("getPosition")?id:simple(p,m,a));}
 static Map<String,Object> run(String raw,String property,String event,boolean before)throws Exception{
  List<URL> urls=new ArrayList<>();if(!raw.isEmpty())for(String u:raw.split("\\|"))urls.add(url(u));DownloadManager target=download(0,urls);
  List<Object> endpoints=new ArrayList<>();for(URL u:urls){Map<String,Object> e=new TreeMap<>();e.put("url",u.toExternalForm());e.put("host",u.getHost());e.put("port",(long)u.getPort());e.put("default_port",(long)u.getDefaultPort());endpoints.add(e);}
  Set<Taggable> members=new LinkedHashSet<>();if(before)members.add(target);List<Object> trace=new ArrayList<>();Tag tag=(Tag)proxy(Tag.class,(p,m,a)->{switch(m.getName()){case "getTagged":return new LinkedHashSet<Taggable>(members);case "hasTaggable":return members.contains(a[0]);case "addTaggable":members.add((Taggable)a[0]);trace.add("add");return null;case "removeTaggable":members.remove(a[0]);trace.add("remove");return null;}return simple(p,m,a);});
  String[] rules=property.isEmpty()?new String[0]:property.split("\\|",-1);TagProperty prop=(TagProperty)proxy(TagProperty.class,(p,m,a)->m.getName().equals("getStringList")?rules:m.getName().equals("getTag")?tag:simple(p,m,a));
  GlobalManager gm=(GlobalManager)proxy(GlobalManager.class,(p,m,a)->m.getName().equals("getDownloadManagers")?Collections.singletonList(target):simple(p,m,a));Core core=(Core)proxy(Core.class,(p,m,a)->m.getName().equals("getGlobalManager")?gm:simple(p,m,a));TagPropertyTrackerHandler handler=(TagPropertyTrackerHandler)U.allocateInstance(TagPropertyTrackerHandler.class);set(handler,TagPropertyTrackerHandler.class,"core",core);set(handler,TagPropertyTrackerHandler.class,"tracker_host_map",new HashMap<String,List<Tag>>());set(handler,TagPropertyTrackerHandler.class,"properties",new HashSet<TagProperty>(Arrays.asList(prop)));set(handler,TagPropertyTrackerHandler.class,"sync_required",new AtomicBoolean(false));
  Method hosts=TagPropertyTrackerHandler.class.getDeclaredMethod("getAugmentedHosts",DownloadManager.class);hosts.setAccessible(true);SortedSet<String> augmented=new TreeSet<>((Set<String>)hosts.invoke(handler,target));Method change=TagPropertyTrackerHandler.class.getDeclaredMethod("handleProperty",TagProperty.class,boolean.class);change.setAccessible(true);
  if(event.equals("changed")){change.invoke(handler,prop,false);}else{change.invoke(handler,prop,true);if(event.equals("created")){Method create=TagPropertyTrackerHandler.class.getDeclaredMethod("handleDownload",DownloadManager.class);create.setAccessible(true);create.invoke(handler,target);}else if(event.equals("sync")){handler.torrentChanged(null,TOTorrentListener.CT_ANNOUNCE_URLS,null);handler.sync();}}
  Map<String,Object> row=new TreeMap<>();row.put("endpoints",endpoints);row.put("property",Arrays.asList(rules));row.put("event",event);row.put("before",before?1L:0L);row.put("after",members.contains(target)?1L:0L);row.put("hosts",new ArrayList<>(augmented));row.put("trace",trace);return row;
 }
 public static void main(String[] args)throws Exception{List<Object> rows=new ArrayList<>();String[] urls={"","http://A.Example.Test/announce","https://a.example.test/announce","udp://a.example.test/announce","udp://a.example.test:0/announce","udp://a.example.test:6969/announce","wss://a.example.test/announce","http://a.example.test.:81/announce","http://127.0.0.1/announce","http://[2001:db8::1]:8080/announce","http://a..example.test/announce","HTTP://A.EXAMPLE.TEST:080/announce","http://a.example.test:+80/announce","http://a.example.test:-1/announce","http://a.example.test:65536/announce","https://[2001:DB8::1]/announce","http://a.example.test/announce?x=1","http://a.example.test/#fragment","http://a.example.test","http://user:generated@a.example.test:81/announce","http://localhost/announce","http://./announce","http:///announce","http://a.example.test/announce|https://other.invalid/announce"};String[] rules={"","EXAMPLE.TEST","test","example.test:80","example.test:443","example.test:6969"," example.test ","[2001:db8::1]:8080","0.1","test.","|example.test"};for(String u:urls)for(String rule:rules)for(String event:new String[]{"startup","created","changed","sync"})for(boolean member:new boolean[]{false,true})rows.add(run(u,rule,event,member));Map<String,Object> out=new TreeMap<>();out.put("rows",rows);Files.write(Paths.get(args[0]),BEncoder.encode(out));System.out.println("ORIGINAL_TRACKER_TAG_RULES rows="+rows.size()+" actual_hosts_property_created_sync_bodies=1 PASS");com.biglybt.core.security.SESecurityManager.exitVM(0);}
}
