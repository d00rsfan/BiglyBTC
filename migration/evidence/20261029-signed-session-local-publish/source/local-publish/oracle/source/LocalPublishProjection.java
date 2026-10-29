import java.lang.reflect.*;
import java.util.*;
import com.biglybt.core.dht.transport.DHTTransportContact;
import com.biglybt.core.dht.db.impl.DHTDBValueImpl;
import com.biglybt.plugin.dht.impl.DHTPluginStorageManager;
import com.biglybt.core.util.AEMonitor;
/** Retained original methods. Only persistent callback and contact are adapters. */
public final class LocalPublishProjection {
 public static final class Subject extends DHTPluginStorageManager {
  private Subject(){super(0,null,null);}
  @Override protected void writeVersionData(){} // Observe the real RAM map; no Java profile IO claim.
 }
 private static Object unsafe;private static Method allocate;private static AEMonitor monitor;
 public static void warm()throws Exception {
  Class<?> u=Class.forName("sun.misc.Unsafe");Field f=u.getDeclaredField("theUnsafe");f.setAccessible(true);unsafe=f.get(null);allocate=u.getMethod("allocateInstance",Class.class);monitor=new AEMonitor("owned-version-projection");
 }
 private static void set(Object value,String field,Object data)throws Exception{Field f=DHTPluginStorageManager.class.getDeclaredField(field);f.setAccessible(true);f.set(value,data);}
 private static String hex(byte[] value){StringBuilder b=new StringBuilder();for(byte v:value)b.append(String.format(java.util.Locale.ROOT,"%02x",v&255));return b.toString();}
 public static void main(String[] ignored)throws Exception {
  if(com.biglybt.core.util.SystemTime.getCurrentTime()/1000>=2100000000L)throw new IllegalStateException("Future-version fixture expired");
  long[] values={2100000000L,2147483600L,(1L<<32)+2100000000L};
  for(int i=0;i<values.length;i++){
   Subject owner=(Subject)allocate.invoke(unsafe,Subject.class);Map<String,Object> map=new HashMap<String,Object>();map.put("next",Long.valueOf(values[i]));map.put("opaque",new byte[]{1,0,-1});set(owner,"version_mon",monitor);set(owner,"version_map",map);
   int first=owner.getNextValueVersions(128);if(!Arrays.equals((byte[])map.get("opaque"),new byte[]{1,0,-1}))throw new AssertionError("Unknown map entry changed");System.out.println("V\t"+i+"\t"+first+"\t"+map.get("next"));
  }
  final byte[] id=new byte[20];Arrays.fill(id,(byte)42);
  DHTTransportContact contact=(DHTTransportContact)Proxy.newProxyInstance(LocalPublishProjection.class.getClassLoader(),new Class<?>[]{DHTTransportContact.class},new InvocationHandler(){public Object invoke(Object p,Method m,Object[] a){if(m.getName().equals("getID"))return id;if(m.getName().equals("toString"))return "owned-contact-adapter";throw new UnsupportedOperationException(m.getName());}});
  Constructor<DHTDBValueImpl> ctor=DHTDBValueImpl.class.getDeclaredConstructor(long.class,byte[].class,int.class,DHTTransportContact.class,DHTTransportContact.class,boolean.class,int.class,int.class,byte.class);ctor.setAccessible(true);
  DHTDBValueImpl value=ctor.newInstance(1000L,new byte[]{97,0,98,-1},123,contact,contact,true,0,0,(byte)-1);
  System.out.println("L\t"+value.getCreationTime()+"\t"+value.getVersion()+"\t"+value.isLocal()+"\t"+value.getFlags()+"\t"+value.getLifeTimeHours()+"\t"+value.getReplicationControl()+"\t"+hex(value.getValue())+"\t"+hex(value.getOriginator().getID()));
 }
}
