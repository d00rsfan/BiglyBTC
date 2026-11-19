import java.nio.*;
import java.nio.file.*;
import java.util.*;
import com.biglybt.core.disk.impl.*;
import com.biglybt.core.diskmanager.cache.*;
import com.biglybt.core.download.*;
import com.biglybt.core.torrent.*;
import com.biglybt.core.util.*;

public final class PopulatedCompactTransitionOracle {
  public static void main(String[] args) throws Exception {
    Path root=Paths.get(args[0]).toAbsolutePath();Files.createDirectories(root);
    Path control=root.resolve("control"),data=root.resolve("data");Files.createDirectories(control);Files.createDirectories(data);
    Path expectedClasses=Paths.get(System.getProperty("oracle.expected.classes")).toRealPath();
    for(Class<?> type:new Class<?>[]{DiskManagerFileInfoImpl.class,Class.forName("com.biglybt.core.diskmanager.file.impl.FMFileAccessController")}){
      if(!Paths.get(type.getProtectionDomain().getCodeSource().getLocation().toURI()).toRealPath().equals(expectedClasses))throw new AssertionError("Unpinned actual class");
    }
    TOTorrent torrent=CompactOwnerOracle.torrent(new long[]{20000,12768},0);
    byte[] edge=new byte[3616];Arrays.fill(edge,(byte)'A');Path payload=data.resolve("f0");Files.write(payload,edge);
    Map<String,Object> saved=new HashMap<>();saved.put("version",0L);saved.put("length",20000L);Files.write(control.resolve("fmfile0.dat"),BEncoder.encode(saved));
    DownloadManagerState state=FilePriorityLoadOracle.proxy(DownloadManagerState.class,(method,a)->{
      if(method.equals("getStateDir"))return control.toFile();
      if(method.equals("getFileLink"))return a.length==2?a[1]:null;
      if(method.equals("getLongAttribute")&&a[0].equals(DownloadManagerState.AT_FILE_ALLOC_STRATEGY))return 0L;
      return FilePriorityLoadOracle.unexpected(method);
    });
    DiskManagerHelper disk=FilePriorityLoadOracle.proxy(DiskManagerHelper.class,(method,a)->{
      if(method.equals("getDownloadState"))return state;
      if(method.equals("getInternalName"))return "Generated populated compact transition";
      if(method.equals("getCacheMode"))return CacheFileOwner.CACHE_MODE_NO_CACHE;
      if(method.equals("getTorrent"))return torrent;
      return FilePriorityLoadOracle.unexpected(method);
    });
    DiskManagerFileInfoImpl file=new DiskManagerFileInfoImpl(disk,data.toFile(),new StringInterner.FileKey("f0"),0,torrent.getFiles()[0],2);
    try{
      byte[] retained=new byte[3616];DirectByteBuffer before=new DirectByteBuffer(ByteBuffer.allocate(retained.length));
      file.getCacheFile().read(before,16384,CacheFile.CP_NONE);before.flip(DirectByteBuffer.SS_FILE);before.get(DirectByteBuffer.SS_FILE,retained);
      if(!Arrays.equals(retained,edge))throw new AssertionError("Actual Original compact boundary read");
      file.getCacheFile().setStorageType(CacheFile.CT_LINEAR,false);
      if(file.getCacheFile().getStorageType()!=CacheFile.CT_LINEAR)throw new AssertionError("Actual converted kind");
      byte[] actual=new byte[20000];DirectByteBuffer after=new DirectByteBuffer(ByteBuffer.allocate(actual.length));
      file.getCacheFile().read(after,0,CacheFile.CP_NONE);after.flip(DirectByteBuffer.SS_FILE);after.get(DirectByteBuffer.SS_FILE,actual);
      if(!Arrays.equals(Arrays.copyOfRange(actual,16384,20000),edge))throw new AssertionError("Converted tail lost");
      if(FilePriorityLoadOracle.unexpectedCalls!=0)throw new AssertionError("Unexpected dependency");
      Files.write(root.resolve("original-linear.bin"),actual);
      System.out.println("PASS_ORIGINAL_POPULATED_COMPACT_TO_LINEAR physical="+Files.size(payload)+" control="+Files.exists(control.resolve("fmfile0.dat")));
    }finally{file.getCacheFile().close();}
  }
}
