// SPDX-License-Identifier: GPL-2.0-or-later
package com.biglybt.core.tag.impl;
import com.biglybt.core.util.*;
import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.util.*;
public final class OriginalBencodeMinimum {
 public static void main(String[] args)throws Exception{
  long[] values={Long.MIN_VALUE,(long)Integer.MIN_VALUE-1,Integer.MIN_VALUE,(long)Integer.MIN_VALUE+1,-2,-1,0,1,Integer.MAX_VALUE,(long)Integer.MAX_VALUE+1,Long.MAX_VALUE};
  List<Object> rows=new ArrayList<>();int failures=0;
  for(long value:values){Map<String,Object> input=Collections.<String,Object>singletonMap("n",value);Map<String,Object> row=new TreeMap<>();row.put("value",Long.toString(value));
   try{byte[] encoded=BEncoder.encode(input);String text=new String(encoded,StandardCharsets.US_ASCII);if(!text.equals("d1:ni"+value+"ee"))throw new AssertionError("Unexpected integer encoding");row.put("encoded",text);row.put("failed",0L);}
   catch(ArrayIndexOutOfBoundsException error){if(value!=Integer.MIN_VALUE)throw error;row.put("failed",1L);row.put("error",error.getClass().getName());failures++;}
   rows.add(row);
  }
  if(failures!=1)throw new AssertionError("Expected one exact original minimum-int failure");
  Files.write(Paths.get(args[0]),BEncoder.encode(Collections.<String,Object>singletonMap("rows",rows)));
  System.out.println("ORIGINAL_BENCODE_MINIMUM cases="+rows.size()+" integer_minimum_crashes="+failures+" actual_encoder=1 PASS");com.biglybt.core.security.SESecurityManager.exitVM(0);
 }
}
