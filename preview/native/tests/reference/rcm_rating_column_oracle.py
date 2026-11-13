from pathlib import Path
import argparse,re,hashlib,json
p=argparse.ArgumentParser();p.add_argument('--source',required=True);p.add_argument('--output',required=True);a=p.parse_args();source=Path(a.source);text=source.read_text();start=text.index('public void refresh(TableCell cell)');mask=re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',lambda m:' '*len(m[0]),text,flags=re.S);brace=mask.index('{',start);depth=0;end=None
for i in range(brace,len(mask)):
 if mask[i]=='{':depth+=1
 elif mask[i]=='}':
  depth-=1
  if depth==0:end=i+1;break
assert end
body=text[start:end]
header='''// Test-only exact original refresh body; explicit memory/English adapters.
import java.io.*;import java.util.*;
public final class RcmRatingColumnOracle {
 static String string(String hex){if(hex.equals("-"))return null;char[] out=new char[hex.length()/4];for(int i=0;i<out.length;i++)out[i]=(char)Integer.parseInt(hex.substring(4*i,4*i+4),16);return new String(out);}
 static String hex(String value){if(value==null)value="";StringBuilder out=new StringBuilder();for(char c:value.toCharArray())out.append(String.format(Locale.ROOT,"%04x",(int)c));return out.toString();}
 static class RelatedContent{byte[] getHash(){return new byte[20];}}
 static class TableCell{String text="...",tooltip="";Object getDataSource(){return new RelatedContent();}Object getSortValue(){return text;}boolean setSortValue(String value){boolean changed=!Objects.equals(text,value);text=value;return changed;}boolean isValid(){return false;}void setToolTip(String value){tooltip=value;}}
 static class RatingsResult{Map result;boolean isComplete(){return true;}Map getResult(){return result;}}
 static class Lookup{RatingsResult result;RatingsResult get(byte[] hash){return result;}}
 static class Dht{boolean isInitialising(){return false;}}
 static class MessageText{static String getString(String key){if(key.equals("label.none"))return "None";if(key.equals("general.na.short"))return "N/A";if(key.equals("rcm.rating.nocomment"))return "<no comment>";if(key.equals("rcm.rating.click"))return "Lookup";if(key.equals("rcm.rating.init"))return "Wait...";throw new IllegalArgumentException(key);}static String getString(String key,String[] values){if(key.equals("rcm.rating.summary"))return values[0]+" out of 5 average score";throw new IllegalArgumentException(key);}}
 Object rating_ipc1=new Object(),rating_ipc2=null;Lookup rating_lookups=new Lookup();Dht dht_plugin=new Dht();
'''
footer='''
 public static void main(String[] args)throws Exception{RcmRatingColumnOracle owner=new RcmRatingColumnOracle();try(BufferedReader input=new BufferedReader(new InputStreamReader(System.in,java.nio.charset.StandardCharsets.US_ASCII))){for(String line;(line=input.readLine())!=null;){RatingsResult result=new RatingsResult();if(!line.equals("NULL")){List<Map> rows=new ArrayList<>();if(!line.equals("EMPTY")){for(String raw:line.split(",",-1)){String[] parts=raw.split(":",2);Map row=new HashMap();row.put("score",Integer.parseInt(parts[0]));row.put("comment",string(parts[1]));rows.add(row);}}result.result=new HashMap();result.result.put("ratings",rows);}owner.rating_lookups.result=result;TableCell cell=new TableCell();try{owner.refresh(cell);System.out.println(hex(cell.text)+"\\t"+hex(cell.tooltip));}catch(NullPointerException failure){System.out.println("ERROR");}}}}
}
'''
out=Path(a.output);out.parent.mkdir(parents=True,exist_ok=True);out.write_text(header+body+footer)
(out.parent/'source-scope.json').write_text(json.dumps({'symbol':'ColumnRC_Rating.refresh','source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'method_sha256':hashlib.sha256(body.encode()).hexdigest(),'scope':'exact original method with explicit memory/English stubs; no SWT/runtime IPC claim'},indent=2)+'\n')
