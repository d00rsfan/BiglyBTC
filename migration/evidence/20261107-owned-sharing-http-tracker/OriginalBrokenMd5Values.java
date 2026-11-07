import com.biglybt.core.util.BrokenMd5Hasher;
import com.biglybt.util.JSONUtils;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.security.MessageDigest;
import java.util.*;
public final class OriginalBrokenMd5Values {
 private static Map<String,Object> map(Object... pairs){Map<String,Object> out=new LinkedHashMap<>();for(int i=0;i<pairs.length;i+=2)out.put((String)pairs[i],pairs[i+1]);return out;}
 private static String hex(byte[] bytes){char[] digits="0123456789ABCDEF".toCharArray();StringBuilder out=new StringBuilder(bytes.length*2);for(byte b:bytes){out.append(digits[(b&255)>>>4]);out.append(digits[b&15]);}return out.toString();}
 private static Map<String,Object> value(String label,byte[] bytes,String utf8)throws Exception{
  return map("label",label,"length",bytes.length,"input_hex",hex(bytes),"input_utf8",utf8,"actual_broken_md5",hex(new BrokenMd5Hasher().calculateHash(bytes)),"standard_md5",hex(MessageDigest.getInstance("MD5").digest(bytes)));
 }
 public static void main(String[] args)throws Exception{
  if(args.length!=1)throw new IllegalArgumentException("One fresh owned absolute output file only");
  List<Map<String,Object>> rows=new ArrayList<>();
  for(int length:new int[]{0,1,55,56,63,64,65,119,120,127,128,129}){byte[] bytes=new byte[length];for(int i=0;i<length;i++)bytes[i]=(byte)(i*37+11);rows.add(value("byte-length-"+length,bytes,null));}
  for(String input:new String[]{"file.bin:1700000000123:7","μ\uD83D\uDE00.bin:1700000000123:7",":a.txt:1700000000123:7:z.txt:1700000000999:9"})rows.add(value("fingerprint-"+rows.size(),input.getBytes(StandardCharsets.UTF_8),input));
  StringBuilder longInput=new StringBuilder();for(int i=0;i<32;i++)longInput.append(":file-").append(i).append(".bin:1700000000123:987654321");rows.add(value("multiblock-directory-fingerprint",longInput.toString().getBytes(StandardCharsets.UTF_8),longInput.toString()));
  if(rows.size()!=16)throw new AssertionError("Fixed16values");
  Path output=Paths.get(args[0]);if(!output.isAbsolute()||Files.exists(output))throw new IllegalArgumentException("Fresh owned absolute output only");
  byte[] report=JSONUtils.encodeToJSON(map("schema","BIGLYBT_ORIGINAL_BROKEN_MD5_PUBLIC_VALUES_V1","Original_commit","cb8c6cb9bb97916cbc7e80338a017200ffc3e71f","case_count",16,"cases",rows)).getBytes(StandardCharsets.UTF_8);
  if(report.length>128*1024)throw new AssertionError("Boundedreport");Files.write(output,report,StandardOpenOption.CREATE_NEW);System.out.println("PASS_ORIGINAL_BROKEN_MD5_PUBLIC_VALUES_16");
 }
}
