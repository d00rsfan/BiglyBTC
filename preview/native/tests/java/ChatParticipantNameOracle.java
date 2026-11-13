// SPDX-License-Identifier: GPL-2.0-or-later
// Exact pinned method only; no plugin constructor, live profile or network.
import java.io.*;
import java.lang.reflect.*;
import java.util.*;
import com.biglybt.plugin.net.buddy.BuddyPluginBeta;
public final class ChatParticipantNameOracle {
 public static void main(String[] args)throws Exception {
  if(args.length!=1)throw new IOException("Generated profile path required");
  System.setProperty("azureus.config.path",args[0]);System.setProperty("user.home",args[0]);
  System.setSecurityManager(new SecurityManager(){public void checkPermission(java.security.Permission p){}public void checkConnect(String h,int p){throw new SecurityException("No network in name oracle");}public void checkListen(int p){throw new SecurityException("No listener in name oracle");}});
  Field field=sun.misc.Unsafe.class.getDeclaredField("theUnsafe");field.setAccessible(true);sun.misc.Unsafe unsafe=(sun.misc.Unsafe)field.get(null);
  Object owner=unsafe.allocateInstance(BuddyPluginBeta.class);Method method=BuddyPluginBeta.class.getDeclaredMethod("pkToString",byte[].class);method.setAccessible(true);
  try(BufferedReader input=new BufferedReader(new InputStreamReader(System.in,java.nio.charset.StandardCharsets.US_ASCII))){for(String line;(line=input.readLine())!=null;){byte[] key=line.equals("-")?null:HexFormat.of().parseHex(line);try{System.out.println((String)method.invoke(owner,(Object)key));}catch(InvocationTargetException e){if(!(e.getCause() instanceof IndexOutOfBoundsException))throw e;System.out.println("ERROR");}}}
 }
}
