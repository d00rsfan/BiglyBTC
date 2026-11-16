// SPDX-License-Identifier: GPL-2.0-or-later
package com.biglybt.core.internat;
import com.biglybt.core.util.BEncoder;
import java.nio.file.*;
import java.util.*;
/** Actual original MessageText, isolated stock bundle, no client/Core. */
public final class OriginalStockTagNames {
 public static void main(String[] args)throws Exception{
  MessageText.changeLocale(Locale.ENGLISH);
  SortedSet<String> keys=new TreeSet<>();
  for(Iterator<?> it=MessageText.RESOURCE_BUNDLE.getKeysLight();it.hasNext();){String k=(String)it.next();if(k.startsWith("tag.")){keys.add(k);}}
  List<Object> rows=new ArrayList<>();int count=0;
  for(String locale:Files.readAllLines(Paths.get(args[1]))){
   if(!MessageText.changeLocale(MessageText.parseFormattedLocaleString(locale))){throw new AssertionError("Original locale unavailable");}
   Map<String,Object> names=new TreeMap<>();for(String key:keys){if(!MessageText.keyExists(key)){throw new AssertionError("Stock key unavailable");}names.put(key,MessageText.getString(key));++count;}
   Map<String,Object> row=new TreeMap<>();row.put("locale",locale);row.put("names",names);rows.add(row);
  }
  Map<String,Object> out=new TreeMap<>();out.put("rows",rows);Files.write(Paths.get(args[0]),BEncoder.encode(out));
  System.out.println("ORIGINAL_STOCK_TAG_NAMES locales="+rows.size()+" keys="+keys.size()+" names="+count+" actual_MessageText=1 PASS");com.biglybt.core.security.SESecurityManager.exitVM(0);
 }
}
