// SPDX-License-Identifier: GPL-2.0-or-later
package com.biglybt.core.dht.db.impl;
import com.biglybt.core.dht.transport.*;
import com.biglybt.core.util.*;
import java.nio.file.*;
import java.util.*;
public final class OriginalLocalRebind {
 static Map<String,Object> run(String name,int[] kinds,int[] versions)throws Exception{
  OriginalCacheRepublish db=new OriginalCacheRepublish();try{
   byte[] key=new byte[20];Arrays.fill(key,(byte)'k');DHTDBMapping mapping=new DHTDBMapping(db,new HashWrapper(key),false);
   for(int i=0;i<kinds.length;i++){int kind=kinds[i];DHTTransportContact origin=db.contact(kind==0?1:kind==1?2:kind==2?3:4);DHTTransportContact sender=kind==3?db.contact(5):origin;DHTDBValueImpl v=new DHTDBValueImpl(123456,new byte[]{(byte)(i+65)},versions[i],origin,sender,kind==0,0x21,72,(byte)-1);mapping.add(v);v.setStoreTime(234567);}
   mapping.updateLocalContact(db.contact(2));List<Object> values=new ArrayList<>();Iterator<DHTDBValueImpl> it=mapping.getValues();while(it.hasNext()){DHTDBValueImpl v=it.next();Map<String,Object> row=new TreeMap<>();row.put("origin",(long)(v.getOriginator().getID()[0]&255));row.put("sender",(long)(v.getSender().getID()[0]&255));row.put("local",v.isLocal()?1L:0L);row.put("version",(long)v.getVersion());row.put("created",v.getCreationTime());row.put("payload",v.getValue());row.put("flags",(long)v.getFlags());row.put("life",(long)v.getLifeTimeHours());row.put("rep",(long)v.getReplicationControl());values.add(row);}values.sort(Comparator.comparingInt(v->((Number)((Map<?,?>)v).get("version")).intValue()));Map<String,Object> result=new TreeMap<>();result.put("name",name);List<Object> input=new ArrayList<>();for(int i=0;i<kinds.length;i++)input.add(Arrays.asList((long)kinds[i],(long)versions[i]));result.put("input",input);result.put("values",values);result.put("local_bytes",(long)mapping.getLocalSize());result.put("direct_bytes",(long)mapping.getDirectSize());result.put("forwarded_bytes",(long)mapping.getIndirectSize());return result;
  }finally{db.destroy();}
 }
 public static void main(String[] args)throws Exception{List<Object> cases=new ArrayList<>();cases.add(run("empty",new int[]{},new int[]{}));cases.add(run("remote-only",new int[]{2,3},new int[]{3,4}));cases.add(run("local",new int[]{0},new int[]{7}));cases.add(run("mixed",new int[]{2,0,3},new int[]{3,7,9}));cases.add(run("newer-local",new int[]{1,0,3},new int[]{4,7,9}));cases.add(run("newer-remote",new int[]{1,0,3},new int[]{10,7,9}));cases.add(run("same-version",new int[]{1,0},new int[]{7,7}));Map<String,Object> output=new TreeMap<>();output.put("cases",cases);Files.write(Paths.get(args[0]),BEncoder.encode(output));System.out.println("ORIGINAL_LOCAL_REBIND cases="+cases.size()+" PASS");com.biglybt.core.security.SESecurityManager.exitVM(0);}
}
