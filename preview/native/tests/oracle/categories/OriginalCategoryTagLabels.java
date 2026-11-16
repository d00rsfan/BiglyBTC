// SPDX-License-Identifier: GPL-2.0-or-later
package com.biglybt.core.internat;
import com.biglybt.core.util.BEncoder;
import java.nio.file.*;
import java.util.*;
public final class OriginalCategoryTagLabels {
 public static void main(String[] args)throws Exception{
  List<Object> rows=new ArrayList<>();for(String locale:Files.readAllLines(Paths.get(args[1]))){
   if(!MessageText.changeLocale(MessageText.parseFormattedLocaleString(locale))){throw new AssertionError("Original locale failed");}
   Map<String,Object> row=new TreeMap<>();row.put("locale",locale);row.put("names",Arrays.asList(MessageText.getString("Categories.all"),MessageText.getString("Categories.uncategorized"),MessageText.getString("TableColumn.header.category")));rows.add(row);
  }
  Map<String,Object> out=new TreeMap<>();out.put("rows",rows);Files.write(Paths.get(args[0]),BEncoder.encode(out));System.out.println("ORIGINAL_CATEGORY_TAG_LABELS locales="+rows.size()+" names="+(rows.size()*3)+" actual_MessageText=1 PASS");com.biglybt.core.security.SESecurityManager.exitVM(0);
 }
}
