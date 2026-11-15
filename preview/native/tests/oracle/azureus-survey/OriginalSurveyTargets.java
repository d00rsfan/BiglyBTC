// SPDX-License-Identifier: GPL-2.0-or-later
package com.biglybt.core.dht.db.impl;
import com.biglybt.core.dht.control.impl.DHTControlImpl;
import com.biglybt.core.util.BEncoder;
import java.lang.reflect.*;
import java.nio.file.*;
import java.util.*;
import sun.misc.Unsafe;
/** Actual deterministic getObfuscatedKey method, no control constructor/IO. */
public final class OriginalSurveyTargets {
 public static void main(String[] args)throws Exception{
  Field f=Unsafe.class.getDeclaredField("theUnsafe");f.setAccessible(true);Unsafe u=(Unsafe)f.get(null);DHTControlImpl control=(DHTControlImpl)u.allocateInstance(DHTControlImpl.class);List<Object> cases=new ArrayList<>();for(int mode=0;mode<5;mode++){byte[] key=new byte[20];for(int i=0;i<20;i++)key[i]=(byte)(mode==0?0:mode==1?255:mode==2?i:mode==3?i%2==0?0:7:20-i);Map<String,Object> row=new TreeMap<>();row.put("input",key);row.put("output",control.getObfuscatedKey(key));cases.add(row);}Map<String,Object> out=new TreeMap<>();out.put("cases",cases);Files.write(Paths.get(args[0]),BEncoder.encode(out));System.out.println("ORIGINAL_SURVEY_TARGETS cases="+cases.size());com.biglybt.core.security.SESecurityManager.exitVM(0);
 }
}
