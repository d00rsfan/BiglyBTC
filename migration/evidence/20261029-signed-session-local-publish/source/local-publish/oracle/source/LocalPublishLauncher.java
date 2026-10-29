import java.io.File;
import java.net.URL;
import java.net.URLClassLoader;
import java.lang.reflect.InvocationTargetException;
public final class LocalPublishLauncher {
 public static void main(String[] args)throws Throwable {
  if(args.length!=2||System.getSecurityManager()!=null)throw new IllegalArgumentException("helper/cohort roots required");
  URL[] roots={new File(args[0]).toURI().toURL(),new File(args[1]).toURI().toURL()};
  try(URLClassLoader isolated=new URLClassLoader(roots,ClassLoader.getSystemClassLoader().getParent())){
   for(String name:new String[]{"com.biglybt.plugin.dht.impl.DHTPluginStorageManager","com.biglybt.core.dht.db.impl.DHTDBValueImpl","com.biglybt.core.util.SystemTime"}){
    Class<?> subject=isolated.loadClass(name);File origin=new File(subject.getProtectionDomain().getCodeSource().getLocation().toURI()).getCanonicalFile();if(!origin.equals(new File(args[1]).getCanonicalFile()))throw new AssertionError("Subject shadowed: "+name);
   }
   // Source-only clock boundary: SystemTime starts its own daemon clock, never
   // Core/router/profile. Initialise that one retained utility before the same
   // no-thread/no-IO fence used by the earlier getter projection.
   isolated.loadClass("com.biglybt.core.util.SystemTime").getMethod("getCurrentTime").invoke(null);
   Class<?> projection=isolated.loadClass("LocalPublishProjection");projection.getMethod("warm").invoke(null);
   QueueOracleFence fence=new QueueOracleFence();System.setSecurityManager(fence);
   try{projection.getMethod("main",String[].class).invoke(null,(Object)new String[0]);}catch(InvocationTargetException failure){throw failure.getCause();}
   if(System.getSecurityManager()!=fence)throw new AssertionError("Fence changed");fence.verify();
  }
 }
}
