// SPDX-License-Identifier: GPL-2.0-or-later
package com.biglybt.core.internat;
import com.biglybt.core.util.BEncoder;
import java.nio.file.*;
import java.util.*;
/** Stock original bundles only; an isolated profile supplies no overrides. */
public final class OriginalDownloadStateLabels {
 public static void main(String[] args)throws Exception{
  String[] keys={"init","down","seed","qford","qfors","stop","err","act","pau","inact","comp","incomp","mov","chk","del"};
  List<Object> rows=new ArrayList<>();int values=0;
  for(String request:Files.readAllLines(Paths.get(args[1]))){
   if(!MessageText.changeLocale(MessageText.parseFormattedLocaleString(request))){throw new AssertionError("Locale load failed: "+request);}
   Map<String,Object> row=new TreeMap<>();row.put("locale",request);row.put("selected",MessageText.getCurrentLocale().toString());
   List<Object> names=new ArrayList<>();for(String key:keys){names.add(MessageText.getString("tag.type.ds."+key));values++;}row.put("names",names);rows.add(row);
  }
  Map<String,Object> result=new TreeMap<>();result.put("rows",rows);Files.write(Paths.get(args[0]),BEncoder.encode(result));
  System.out.println("ORIGINAL_DOWNLOAD_STATE_LABELS locales="+rows.size()+" names="+values+" actual_MessageText=1 PASS");
  com.biglybt.core.security.SESecurityManager.exitVM(0);
 }
}
