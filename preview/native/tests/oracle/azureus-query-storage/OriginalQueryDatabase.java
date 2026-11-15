// SPDX-License-Identifier: GPL-2.0-or-later
package com.biglybt.core.dht.db.impl;
import com.biglybt.core.dht.control.*;
import com.biglybt.core.dht.control.impl.DHTControlImpl;
import com.biglybt.core.util.*;
import java.lang.reflect.*;
import java.nio.file.*;
import java.util.*;

/** Actual original prefix-map and queryStore methods, including collisions. */
public final class OriginalQueryDatabase {
 static String hex(byte[] b){if(b==null)return "-";StringBuilder s=new StringBuilder();for(byte x:b)s.append(String.format("%02x",x&255));return s.toString();}
 public static void main(String[] args)throws Exception{
  List<String> lines=new ArrayList<>();for(boolean reverse:new boolean[]{false,true}){OriginalCacheRepublish db=new OriginalCacheRepublish();try{
   Field field=DHTDBImpl.class.getDeclaredField("control");field.setAccessible(true);DHTControl old=(DHTControl)field.get(db);Method compare=DHTControlImpl.class.getDeclaredMethod("computeAndCompareDistances2",byte[].class,byte[].class,byte[].class);compare.setAccessible(true);
   db.setControl(OriginalCacheRepublish.proxy(DHTControl.class,(p,m,a)->m.getName().equals("computeAndCompareDistances")?compare.invoke(null,a):m.invoke(old,a)));
   byte[] near=new byte[20];Arrays.fill(near,(byte)'P');near[6]=0;near[18]='a';near[19]='1';byte[] far=near.clone();far[6]=(byte)255;far[18]='b';far[19]='2';
   String[] actions={"empty",reverse?"putNear":"putFar",reverse?"putFar":"putNear","removeFar","putFar","removeNear","putNear","removeFar","removeNear"};
   for(String action:actions){byte[] key=action.endsWith("Near")?near:far;HashWrapper hw=new HashWrapper(key);if(action.startsWith("put")){DHTDBMapping mapping=new DHTDBMapping(db,hw,false);db.mappings().put(hw,mapping);db.addToPrefixMap(mapping);}else if(action.startsWith("remove")){DHTDBMapping mapping=db.mappings().remove(hw);if(mapping!=null)db.removeFromPrefixMap(mapping);}
    List<Object[]> groups=new ArrayList<>();groups.add(new Object[]{Arrays.copyOf(near,3),Arrays.asList(Arrays.copyOfRange(near,3,6))});byte[] row=db.queryStore(db.destination,6,groups).getEntries().get(0);lines.add((reverse?"reverse":"forward")+"\t"+action+"\t"+hex(row));
   }
  }finally{db.destroy();}}
  Files.write(Paths.get(args[0]),lines);System.out.println("ORIGINAL_QUERY_DATABASE transitions="+lines.size());com.biglybt.core.security.SESecurityManager.exitVM(0);
 }
}
