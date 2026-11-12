// SPDX-License-Identifier: GPL-2.0-or-later
import java.io.*;
import java.nio.file.*;
import java.util.*;
import java.util.zip.*;
import com.biglybt.core.util.BDecoder;
public final class ResumeStateOracle {
 public static void main(String[] args) throws Exception {
  byte[] raw=Files.readAllBytes(Paths.get(args[0]));
  if(raw.length>1&&(raw[0]&255)==31&&(raw[1]&255)==139){
   ByteArrayOutputStream out=new ByteArrayOutputStream();
   try(InputStream in=new GZIPInputStream(new ByteArrayInputStream(raw))){byte[] buf=new byte[8192];for(int n;(n=in.read(buf))>=0;){out.write(buf,0,n);}}raw=out.toByteArray();
  }
  Map root=BDecoder.decode(raw);Map resume=(Map)((Map)root.get("resume")).get("data");byte[] pieces=(byte[])resume.get("resume data");StringBuilder hex=new StringBuilder();for(byte b:pieces){hex.append(String.format("%02x",b&255));}
  if(!hex.toString().equals(args[1])||((Number)resume.get("valid")).longValue()!=Long.parseLong(args[2])){throw new AssertionError("Original Java resume map differs");}
 }
}
