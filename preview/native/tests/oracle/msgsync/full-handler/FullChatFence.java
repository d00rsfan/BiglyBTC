// SPDX-License-Identifier: GPL-2.0-or-later
import java.io.*;import java.net.*;import java.nio.file.*;import java.security.Permission;
/** JDK17 reference tests: generated build tree only, no sockets/children/JNI escapes. */
public final class FullChatFence extends SecurityManager {
 final String root,classes;
 public FullChatFence()throws Exception{
  root=new File(System.getProperty("biglybtc.test.root")).getCanonicalPath();classes=new File(System.getProperty("biglybtc.test.classes")).getCanonicalPath();
  Path marker=Paths.get(root,".owned-chat-oracle");if(!root.contains(File.separator+"build"+File.separator)||root.contains(File.separator+".biglybt")||Files.isSymbolicLink(marker)||!new String(Files.readAllBytes(marker),"UTF-8").equals("BiglyBTC generated reference only\n")){throw new SecurityException("Generated build-owned reference root required");}
 }
 String canonical(String path){try{return new File(path).getCanonicalPath();}catch(Exception e){throw new SecurityException("Unresolvable reference path");}}
 boolean under(String value,String base){return value.equals(base)||value.startsWith(base+File.separator);}
 void writable(String path){if(!under(canonical(path),root)){throw new SecurityException("Write outside generated reference root");}}
 @Override public void checkRead(String path){String value=canonical(path);if((value.startsWith("/home/")||value.startsWith("/root/")||value.startsWith("/mnt/")||value.startsWith("/media/")||value.startsWith("/run/user/"))&&!under(value,root)&&!under(value,classes)){throw new SecurityException("Personal data excluded from reference tests");}}
 @Override public void checkRead(String path,Object context){checkRead(path);}
 @Override public void checkWrite(String path){writable(path);}@Override public void checkDelete(String path){writable(path);}
 @Override public void checkConnect(String host,int port){throw new SecurityException("Sockets excluded from pipe reference tests");}
 @Override public void checkConnect(String host,int port,Object context){checkConnect(host,port);}
 @Override public void checkListen(int port){throw new SecurityException("Listeners excluded from pipe reference tests");}
 @Override public void checkAccept(String host,int port){throw new SecurityException("Sockets excluded from pipe reference tests");}
 @Override public void checkMulticast(InetAddress address){throw new SecurityException("Multicast excluded");}
 @Override public void checkExec(String command){throw new SecurityException("Child execution excluded");}
 @Override public void checkLink(String name){if(!name.equals("net")&&!name.equals("nio")&&!name.equals("zip")&&!name.equals("management")&&!name.equals("extnet")){throw new SecurityException("Third-party JNI excluded");}}
 @Override public void checkPermission(Permission p){if(p instanceof RuntimePermission&&p.getName().equals("setSecurityManager")){throw new SecurityException("Reference guard cannot be replaced");}if(p instanceof LinkPermission){throw new SecurityException("Links excluded");}if(p instanceof FilePermission){String a=p.getActions();if(a.contains("write")||a.contains("delete")){writable(p.getName());}if(a.contains("execute")){throw new SecurityException("Execution excluded");}}}
 @Override public void checkPermission(Permission p,Object context){checkPermission(p);}
}
