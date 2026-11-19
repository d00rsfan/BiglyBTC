import java.io.*;
import java.nio.*;
import java.nio.file.*;
import java.security.*;
import java.util.*;
import java.util.zip.GZIPInputStream;
import com.biglybt.core.disk.impl.*;
import com.biglybt.core.diskmanager.cache.*;
import com.biglybt.core.download.*;
import com.biglybt.core.torrent.*;
import com.biglybt.core.util.*;

public final class SelectionCacheReadOracle {
  public static void main(String[] args) throws Exception {
    if(args.length!=6)throw new IllegalArgumentException("Generated state/root/control/index/kind/hash required");
    Path stateFile=Paths.get(args[0]).toRealPath();Path root=stateFile.getParent().getParent().getParent();
    if(!root.getParent().equals(Paths.get("/tmp"))||!root.getFileName().toString().startsWith("biglybtc-file-policy-"))throw new IllegalArgumentException("Generated fixture only");
    Path payload=Paths.get(args[1]).toRealPath(),control=Paths.get(args[2]).toAbsolutePath().normalize();
    if(!payload.startsWith(root)||!control.startsWith(root))throw new IllegalArgumentException("Generated fixture bounds");
    Files.createDirectories(control);
    Path expected=Paths.get(System.getProperty("oracle.expected.classes")).toRealPath();
    if(!Paths.get(DiskManagerFileInfoImpl.class.getProtectionDomain().getCodeSource().getLocation().toURI()).toRealPath().equals(expected))throw new AssertionError("Unpinned original constructor");
    byte[] raw=Files.readAllBytes(stateFile);
    if(raw.length>1&&(raw[0]&255)==31&&(raw[1]&255)==139){
      try(GZIPInputStream gzip=new GZIPInputStream(new ByteArrayInputStream(raw));ByteArrayOutputStream out=new ByteArrayOutputStream()){
        byte[] block=new byte[4096];for(int n;(n=gzip.read(block))>=0;)out.write(block,0,n);raw=out.toByteArray();
      }
    }
    TOTorrent torrent=TOTorrentFactory.deserialiseFromBEncodedByteArray(new TOTorrentFactory.TorrentDataHolder(raw));
    int index=Integer.parseInt(args[3]),kind=Integer.parseInt(args[4]);
    Map attrs=torrent.getAdditionalMapProperty("attributes");
    List types=(List)attrs.get("storetypes");
    String persisted=new String((byte[])types.get(index),"UTF-8");
    if(!persisted.equals(kind==1?"L":"R"))throw new AssertionError("Native persisted requested kind");
    DownloadManagerState state=FilePriorityLoadOracle.proxy(DownloadManagerState.class,(method,a)->{
      if(method.equals("getStateDir"))return control.toFile();
      if(method.equals("getFileLink"))return a.length==2?a[1]:null;
      if(method.equals("getLongAttribute")&&a[0].equals(DownloadManagerState.AT_FILE_ALLOC_STRATEGY))return 0L;
      return FilePriorityLoadOracle.unexpected(method);
    });
    DiskManagerHelper disk=FilePriorityLoadOracle.proxy(DiskManagerHelper.class,(method,a)->{
      if(method.equals("getDownloadState"))return state;
      if(method.equals("getInternalName"))return "GeneratedSelectionRead";
      if(method.equals("getCacheMode"))return CacheFileOwner.CACHE_MODE_NO_CACHE;
      if(method.equals("getTorrent"))return torrent;
      return FilePriorityLoadOracle.unexpected(method);
    });
    TOTorrentFile metadata=torrent.getFiles()[index];
    if(metadata.getLength()<0||metadata.getLength()>16384)throw new IllegalArgumentException("Bounded identity fixture");
    DiskManagerFileInfoImpl file=new DiskManagerFileInfoImpl(disk,payload.toFile(),new StringInterner.FileKey(metadata.getRelativePath()),index,metadata,kind);
    try{
      if(file.isSkipped()||file.getCacheFile().getStorageType()!=kind)throw new AssertionError("Original requested storage/selected constructor facts");
      byte[] actual=new byte[(int)metadata.getLength()];
      DirectByteBuffer buffer=new DirectByteBuffer(ByteBuffer.allocate(actual.length));
      file.getCacheFile().read(buffer,0,CacheFile.CP_NONE);buffer.flip(DirectByteBuffer.SS_FILE);buffer.get(DirectByteBuffer.SS_FILE,actual);
      byte[] digest=MessageDigest.getInstance("SHA-256").digest(actual);StringBuilder hex=new StringBuilder();for(byte value:digest)hex.append(String.format("%02x",value&255));
      if(!hex.toString().equals(args[5]))throw new AssertionError("Actual original cache read differs from pre-action bytes");
      if(FilePriorityLoadOracle.unexpectedCalls!=0)throw new AssertionError("Unexpected dependency");
      System.out.println("PASS_ORIGINAL_SELECTION_CACHE_READ kind="+kind+" bytes="+actual.length);
    }finally{file.getCacheFile().close();}
  }
}
