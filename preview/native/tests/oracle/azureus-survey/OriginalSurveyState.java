// SPDX-License-Identifier: GPL-2.0-or-later
package com.biglybt.core.dht.db.impl;
import com.biglybt.core.dht.transport.*;
import com.biglybt.core.util.*;
import java.lang.reflect.*;
import java.nio.file.*;
import java.util.*;
import sun.misc.Unsafe;

/** Actual original SurveyContactState methods; generated clock and mappings. */
public final class OriginalSurveyState {
 static long now;static final List<String> rows=new ArrayList<>();
 static final Unsafe U;
 static {try{Field f=Unsafe.class.getDeclaredField("theUnsafe");f.setAccessible(true);U=(Unsafe)f.get(null);}catch(Exception e){throw new RuntimeException(e);}}
 static long field(DHTDBImpl.SurveyContactState state,String name)throws Exception{Field f=DHTDBImpl.SurveyContactState.class.getDeclaredField(name);f.setAccessible(true);return f.getLong(state);}
 static DHTTransportContact contact(OriginalCacheRepublish db,int instance){DHTTransportContact old=db.contact(9);return OriginalCacheRepublish.proxy(DHTTransportContact.class,(p,m,a)->m.getName().equals("getInstanceID")?instance:m.invoke(old,a));}
 static void row(String scenario,String action,DHTDBImpl.SurveyContactState state,DHTDBMapping[] mappings,long jitter)throws Exception{
  int mask=0;for(int i=0;i<mappings.length;i++){if(state.testMapping(mappings[i]))mask|=1<<i;}
  rows.add(scenario+"\t"+action+"\t"+now+"\t"+jitter+"\t"+state.getContact().getInstanceID()+"\t"+state.getCreationTime()+"\t"+field(state,"timeout")+"\t"+state.getLastUseTime()+"\t"+state.getConsecFails()+"\t"+mask+"\t"+(state.timeout(now)?1:0));
 }
 static void run(String scenario,long start)throws Exception{
  now=start;OriginalCacheRepublish db=new OriginalCacheRepublish();try{DHTDBMapping[] mappings=new DHTDBMapping[3];byte[] key=new byte[20];key[0]=42;mappings[0]=new DHTDBMapping(db,new HashWrapper(key.clone()),false);key[0]=43;mappings[1]=new DHTDBMapping(db,new HashWrapper(key.clone()),false);key[0]=42;mappings[2]=new DHTDBMapping(db,new HashWrapper(key.clone()),false);
   RandomUtils.RANDOM.setSeed(12345);long jitter=new Random(12345).nextInt(3600000);DHTDBImpl.SurveyContactState state=new DHTDBImpl.SurveyContactState(contact(db,1));if(field(state,"timeout")!=start+12600000+jitter)throw new AssertionError("Original state jitter");row(scenario,"create",state,mappings,jitter);
   state.addMapping(mappings[0]);row(scenario,"add0",state,mappings,jitter);state.addMapping(mappings[0]);row(scenario,"add0",state,mappings,jitter);state.addMapping(mappings[1]);row(scenario,"add1",state,mappings,jitter);state.contactFailed();row(scenario,"fail",state,mappings,jitter);state.contactOK();row(scenario,"ok",state,mappings,jitter);state.contactFailed();row(scenario,"fail",state,mappings,jitter);state.contactFailed();row(scenario,"fail",state,mappings,jitter);state.contactFailed();row(scenario,"fail",state,mappings,jitter);
   state.addMapping(mappings[0]);row(scenario,"add0",state,mappings,jitter);state.addMapping(mappings[2]);row(scenario,"add2",state,mappings,jitter);if(state.getMappings().size()!=2)throw new AssertionError("Original object identity or empty membership");state.removeMapping(mappings[0]);row(scenario,"remove0",state,mappings,jitter);state.updateContactDetails(contact(db,1));row(scenario,"instance",state,mappings,jitter);state.updateContactDetails(contact(db,2));row(scenario,"instance",state,mappings,jitter);state.addMapping(mappings[1]);row(scenario,"add1",state,mappings,jitter);
   now=start+123;state.updateUseTime();row(scenario,"use",state,mappings,jitter);now=state.getLastUseTime()+3600000;row(scenario,"probe",state,mappings,jitter);now++;row(scenario,"probe",state,mappings,jitter);now=start-1000;row(scenario,"probe",state,mappings,jitter);now=field(state,"timeout");state.updateUseTime();row(scenario,"use",state,mappings,jitter);row(scenario,"probe",state,mappings,jitter);now++;row(scenario,"probe",state,mappings,jitter);
  }finally{db.destroy();}
 }
 public static void main(String[] args)throws Exception{
  SystemTime.getCurrentTime();Field f=SystemTime.class.getDeclaredField("instance");f.setAccessible(true);Object original=f.get(null);Class<?> type=Class.forName("com.biglybt.core.util.SystemTime$SystemTimeProvider");Object clock=java.lang.reflect.Proxy.newProxyInstance(type.getClassLoader(),new Class<?>[]{type},(p,m,a)->now);U.putObjectVolatile(U.staticFieldBase(f),U.staticFieldOffset(f),clock);
  try{run("normal",5000000);run("creation-overflow",Long.MAX_VALUE-1000);}finally{U.putObjectVolatile(U.staticFieldBase(f),U.staticFieldOffset(f),original);}
  Files.write(Paths.get(args[0]),rows);System.out.println("ORIGINAL_SURVEY_STATE transitions="+rows.size());com.biglybt.core.security.SESecurityManager.exitVM(0);
 }
}
