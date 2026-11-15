// SPDX-License-Identifier: GPL-2.0-or-later
package com.biglybt.core.dht.db.impl;
import com.biglybt.core.util.*;
import java.lang.reflect.*;
import java.nio.file.*;
import java.util.*;
import sun.misc.Unsafe;

/** Actual applyRF method, isolated generated JVM with controlled clock/RNG. */
public final class OriginalSurveyEligibility {
 static long wall,mono;static int wallCalls,monoCalls;static final List<String> lines=new ArrayList<>();
 static final Unsafe U;
 static {try{Field f=Unsafe.class.getDeclaredField("theUnsafe");f.setAccessible(true);U=(Unsafe)f.get(null);}catch(Exception e){throw new RuntimeException(e);}}
 @SuppressWarnings("unchecked")
 static void run(String name,int div,long w,long m,Long previous,int... reps)throws Exception{
  wall=w;mono=m;OriginalCacheRepublish db=new OriginalCacheRepublish();try{byte[] keyBytes=new byte[20];keyBytes[0]=42;HashWrapper key=new HashWrapper(keyBytes);DHTDBMapping mapping=new DHTDBMapping(db,key,false);StringJoiner input=new StringJoiner(",");
   for(int i=0;i<reps.length;i++){long created=name.equals("wall-age-overflow")?Long.MIN_VALUE+10:wall-(i+1)*600000L;DHTDBValueImpl value=new DHTDBValueImpl(created,new byte[]{(byte)(10+i)},i+1,db.contact(i+3),db.contact(i+3),i%2==0,0,72,(byte)reps[i]);mapping.add(value);input.add(reps[i]+":"+value.getCreationTime());}
   Field d=DHTDBMapping.class.getDeclaredField("diversification_state");d.setAccessible(true);d.setByte(mapping,(byte)div);Field t=DHTDBImpl.class.getDeclaredField("survey_mapping_times");t.setAccessible(true);Map<HashWrapper,Long> times=(Map<HashWrapper,Long>)t.get(db);if(previous!=null)times.put(key,previous);
   long seed=12345;RandomUtils.RANDOM.setSeed(seed);Random reference=new Random(seed);int jitter=reference.nextInt(1800000)-900000;int afterDraw=reference.nextInt();int withoutDraw=new Random(seed).nextInt();wallCalls=monoCalls=0;boolean eligible=db.applyRF(mapping);int actualWallCalls=wallCalls,actualMonoCalls=monoCalls;int probe=RandomUtils.RANDOM.nextInt();boolean drawn=probe==afterDraw;if(!drawn&&probe!=withoutDraw)throw new AssertionError("Unexpected original RNG consumption");Long next=times.get(key);
   lines.add(name+"\t"+div+"\t"+wall+"\t"+mono+"\t"+(previous==null?"-":previous)+"\t"+jitter+"\t"+(reps.length==0?"-":input)+"\t"+(eligible?1:0)+"\t"+(next==null?"-":next)+"\t"+actualMonoCalls+"\t"+actualWallCalls+"\t"+(drawn?1:0));
  }finally{db.destroy();}
 }
 public static void main(String[] args)throws Exception{
  // Initialise the original clock before replacing only this generated JVM's provider.
  SystemTime.getCurrentTime();Field f=SystemTime.class.getDeclaredField("instance");f.setAccessible(true);Object original=f.get(null);Class<?> type=Class.forName("com.biglybt.core.util.SystemTime$SystemTimeProvider");Object clock=java.lang.reflect.Proxy.newProxyInstance(type.getClassLoader(),new Class<?>[]{type},(p,m,a)->{if(m.getName().equals("getTime")){wallCalls++;return wall;}monoCalls++;return mono;});U.putObjectVolatile(U.staticFieldBase(f),U.staticFieldOffset(f),clock);
  try{long w=100000000,m=5000000;run("empty",1,w,m,null);run("default",1,w,m,null,-1);run("zero",1,w,m,null,0);run("zero-factor-timed",1,w,m,null,16);run("frequency-zero",1,w,m,null,1);run("frequency-zero-largest-factor",1,w,m,null,15);run("frequency-zero-ignores-old-deadline",1,w,m,m+1000,1,33);run("first-frequency-one",1,w,m,null,17);run("first-frequency-two",1,w,m,null,33);run("first-frequency-fifteen",1,w,m,null,243);run("mixed-frequency-minimum",1,w,m,null,49,34,19);run("mixed-default",1,w,m,null,17,-1);run("mixed-zero-factor",1,w,m,null,17,32);run("diversified-frequency",2,w,m,null,17);run("diversified-size",3,w,m,null,17);run("future-next",1,w,m,m+1,17);run("exact-next",1,w,m,m,17);run("past-next",1,w,m,m-1,17);run("oldest-creation-controls-phase",1,w,m,m-1,33,34,35);run("wall-wrap",1,Long.MIN_VALUE+50,m,null,17);run("wall-age-overflow",1,Long.MAX_VALUE-50,m,null,17);run("mono-wrap",1,w,Long.MAX_VALUE-100,null,17);run("negative-mono",1,w,-5000000L,Long.MIN_VALUE,17);run("signed-default-byte",1,w,m,null,255);
  }finally{U.putObjectVolatile(U.staticFieldBase(f),U.staticFieldOffset(f),original);}
  Files.write(Paths.get(args[0]),lines);System.out.println("ORIGINAL_SURVEY_ELIGIBILITY cases="+lines.size());com.biglybt.core.security.SESecurityManager.exitVM(0);
 }
}
