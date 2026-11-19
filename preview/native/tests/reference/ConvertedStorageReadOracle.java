import java.io.*;
import java.nio.*;
import java.nio.file.*;
import java.security.*;
import java.util.*;
import com.biglybt.core.disk.impl.*;
import com.biglybt.core.diskmanager.cache.*;
import com.biglybt.core.download.*;
import com.biglybt.core.torrent.*;
import com.biglybt.core.util.*;

public final class ConvertedStorageReadOracle {
  public static void main(String[] args)throws Exception {
    if(args.length!=5)throw new IllegalArgumentException("Fixture state/data/control/kind/hash");
    Path stateFile=Paths.get(args[0]).toRealPath(),data=Paths.get(args[1]).toRealPath(),control=Paths.get(args[2]).toRealPath();int kind=Integer.parseInt(args[3]);
    if(!data.getParent().equals(stateFile.getParent())||!control.getParent().equals(stateFile.getParent())||(kind!=1&&kind!=3))throw new IllegalArgumentException("Generated emitted fixture only");
    Path expected=Paths.get(System.getProperty("oracle.expected.classes")).toRealPath();
    for(Class<?> type:new Class<?>[]{DiskManagerFileInfoImpl.class,Class.forName("com.biglybt.core.diskmanager.file.impl.FMFileAccessController")})if(!Paths.get(type.getProtectionDomain().getCodeSource().getLocation().toURI()).toRealPath().equals(expected))throw new AssertionError("Unpinned actual reader");
    TOTorrent torrent=TOTorrentFactory.deserialiseFromBEncodedByteArray(new TOTorrentFactory.TorrentDataHolder(Files.readAllBytes(stateFile)));
    Map attrs=torrent.getAdditionalMapProperty("attributes");List types=(List)attrs.get("storetypes");String persisted=new String((byte[])types.get(0),"UTF-8");if(!persisted.equals(kind==1?"L":"R"))throw new AssertionError("Native requested type");
    DownloadManagerState state=FilePriorityLoadOracle.proxy(DownloadManagerState.class,(method,a)->{
      if(method.equals("getStateDir"))return control.toFile();if(method.equals("getFileLink"))return a.length==2?a[1]:null;
      if(method.equals("getLongAttribute")&&a[0].equals(DownloadManagerState.AT_FILE_ALLOC_STRATEGY))return 0L;
      return FilePriorityLoadOracle.unexpected(method);
    });
    DiskManagerHelper disk=FilePriorityLoadOracle.proxy(DiskManagerHelper.class,(method,a)->{
      if(method.equals("getDownloadState"))return state;if(method.equals("getInternalName"))return "Generated Native converted output";
      if(method.equals("getCacheMode"))return CacheFileOwner.CACHE_MODE_NO_CACHE;if(method.equals("getTorrent"))return torrent;
      return FilePriorityLoadOracle.unexpected(method);
    });
    TOTorrentFile metadata=torrent.getFiles()[0];StringJoiner relative=new StringJoiner(File.separator);for(byte[] part:metadata.getPathComponents())relative.add(new String(part,"UTF-8"));
    DiskManagerFileInfoImpl file=new DiskManagerFileInfoImpl(disk,data.toFile(),new StringInterner.FileKey(relative.toString()),0,metadata,kind);
    try{
      if(file.isSkipped()||file.getCacheFile().getStorageType()!=kind)throw new AssertionError("Actual converted requested kind/constructor");
      int[] offsets=kind==1?new int[]{0}:new int[]{0,16384,32768,65536};int[] lengths=kind==1?new int[]{20000}:new int[]{16384,16384,16384,4464};MessageDigest digest=MessageDigest.getInstance("SHA-256");
      for(int i=0;i<offsets.length;i++){byte[] block=new byte[lengths[i]];DirectByteBuffer buffer=new DirectByteBuffer(ByteBuffer.allocate(block.length));file.getCacheFile().read(buffer,offsets[i],CacheFile.CP_NONE);buffer.flip(DirectByteBuffer.SS_FILE);buffer.get(DirectByteBuffer.SS_FILE,block);digest.update(block);}
      StringBuilder hex=new StringBuilder();for(byte value:digest.digest())hex.append(String.format("%02x",value&255));if(!hex.toString().equals(args[4]))throw new AssertionError("Actual Original readback differs");
      if(FilePriorityLoadOracle.unexpectedCalls!=0)throw new AssertionError("Unexpected reader dependency");
      System.out.println("PASS_ORIGINAL_CONVERTED_READ kind="+kind+" ranges="+offsets.length);
    }finally{file.getCacheFile().close();}
  }
}
