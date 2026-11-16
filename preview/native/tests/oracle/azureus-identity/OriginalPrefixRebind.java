// SPDX-License-Identifier: GPL-2.0-or-later
package com.biglybt.core.dht.db.impl;
import com.biglybt.core.dht.control.*;
import com.biglybt.core.dht.control.impl.DHTControlImpl;
import com.biglybt.core.dht.router.*;
import com.biglybt.core.dht.transport.*;
import com.biglybt.core.util.*;
import java.lang.reflect.*;
import java.nio.file.*;
import java.util.*;
public final class OriginalPrefixRebind {
 public static void main(String[] args)throws Exception{OriginalCacheRepublish db=new OriginalCacheRepublish();try{Field controlField=DHTDBImpl.class.getDeclaredField("control");controlField.setAccessible(true);DHTControl original=(DHTControl)controlField.get(db);Method compare=DHTControlImpl.class.getDeclaredMethod("computeAndCompareDistances2",byte[].class,byte[].class,byte[].class);compare.setAccessible(true);final byte[] id=db.localId.clone();DHTRouter router=OriginalCacheRepublish.proxy(DHTRouter.class,(p,m,a)->m.getName().equals("getID")?id:OriginalCacheRepublish.defaults(m));DHTTransportContact local=OriginalCacheRepublish.proxy(DHTTransportContact.class,(p,m,a)->m.getName().equals("getID")?id:m.invoke(db.local,a));DHTTransport transport=OriginalCacheRepublish.proxy(DHTTransport.class,(p,m,a)->m.getName().equals("getLocalContact")?local:OriginalCacheRepublish.defaults(m));DHTControl control=OriginalCacheRepublish.proxy(DHTControl.class,(p,m,a)->m.getName().equals("computeAndCompareDistances")?compare.invoke(null,a):m.getName().equals("getRouter")?router:m.getName().equals("getTransport")?transport:m.invoke(original,a));db.setControl(control);byte[] near=new byte[20];Arrays.fill(near,(byte)'P');near[6]=0;near[18]='a';near[19]='1';byte[] far=near.clone();far[6]=(byte)255;far[18]='b';far[19]='2';for(byte[] key:new byte[][]{near,far}){DHTDBMapping mapping=new DHTDBMapping(db,new HashWrapper(key),false);db.mappings().put(new HashWrapper(key),mapping);db.addToPrefixMap(mapping);}List<Object[]> groups=Arrays.<Object[]>asList(new Object[]{Arrays.copyOf(near,3),Arrays.asList(Arrays.copyOfRange(near,3,6))});byte[] before=db.queryStore(db.destination,6,groups).getEntries().get(0);id[6]=(byte)255;db.setControl(control);byte[] after=db.queryStore(db.destination,6,groups).getEntries().get(0);if(!Arrays.equals(before,after)||!Arrays.equals(before,new byte[]{'a','1'}))throw new AssertionError("Original prefix winner changed");Files.write(Paths.get(args[0]),Arrays.asList("before=a1","after=a1"));System.out.println("ORIGINAL_PREFIX_REBIND existing_winner_retained=1 PASS");}finally{db.destroy();}com.biglybt.core.security.SESecurityManager.exitVM(0);}
}
