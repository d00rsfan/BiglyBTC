import java.nio.*;
import java.nio.file.*;
import java.util.*;
import com.biglybt.core.disk.impl.*;
import com.biglybt.core.diskmanager.cache.*;
import com.biglybt.core.download.*;
import com.biglybt.core.torrent.*;
import com.biglybt.core.util.*;

public final class PopulatedReorderedTransitionOracle {
  static void checkRead(CacheFile file,int offset,int count,byte value)throws Exception {
    byte[] actual=new byte[count];DirectByteBuffer buffer=new DirectByteBuffer(ByteBuffer.allocate(count));
    file.read(buffer,offset,CacheFile.CP_NONE);buffer.flip(DirectByteBuffer.SS_FILE);buffer.get(DirectByteBuffer.SS_FILE,actual);
    for(byte b:actual)if(b!=value)throw new AssertionError("Actual Original reordered logical read");
  }
  public static void main(String[] args)throws Exception {
    Path root=Paths.get(args[0]).toAbsolutePath();Files.createDirectories(root);
    Path control=root.resolve("control"),data=root.resolve("data");Files.createDirectories(control);Files.createDirectories(data);
    Path expected=Paths.get(System.getProperty("oracle.expected.classes")).toRealPath();
    for(Class<?> type:new Class<?>[]{DiskManagerFileInfoImpl.class,Class.forName("com.biglybt.core.diskmanager.file.impl.FMFileAccessController")}){
      if(!Paths.get(type.getProtectionDomain().getCodeSource().getLocation().toURI()).toRealPath().equals(expected))throw new AssertionError("Unpinned actual class");
    }
    TOTorrent torrent=CompactOwnerOracle.torrent(new long[]{70000,11920},0);
    DownloadManagerState state=FilePriorityLoadOracle.proxy(DownloadManagerState.class,(method,a)->{
      if(method.equals("getStateDir"))return control.toFile();
      if(method.equals("getFileLink"))return a.length==2?a[1]:null;
      if(method.equals("getLongAttribute")&&a[0].equals(DownloadManagerState.AT_FILE_ALLOC_STRATEGY))return 0L;
      return FilePriorityLoadOracle.unexpected(method);
    });
    DiskManagerHelper disk=FilePriorityLoadOracle.proxy(DiskManagerHelper.class,(method,a)->{
      if(method.equals("getDownloadState"))return state;
      if(method.equals("getInternalName"))return "Generated populated reordered transition";
      if(method.equals("getCacheMode"))return CacheFileOwner.CACHE_MODE_NO_CACHE;
      if(method.equals("getTorrent"))return torrent;
      return FilePriorityLoadOracle.unexpected(method);
    });
    DiskManagerFileInfoImpl file=new DiskManagerFileInfoImpl(disk,data.toFile(),new StringInterner.FileKey("f0"),0,torrent.getFiles()[0],4);
    try{
      CacheFile cache=file.getCacheFile();cache.setAccessMode(CacheFile.CF_WRITE);
      int[] offsets={32768,16384,0,65536};byte[] values={'C','B','A','E'};
      for(int i=0;i<offsets.length;i++){
        byte[] block=new byte[Math.min(16384,70000-offsets[i])];Arrays.fill(block,values[i]);
        cache.write(new DirectByteBuffer(ByteBuffer.wrap(block)),offsets[i]);
      }
      cache.flushCache();long length=cache.getLength();
      for(int i=0;i<offsets.length;i++)checkRead(cache,offsets[i],Math.min(16384,70000-offsets[i]),values[i]);
      Path map=control.resolve("fmfile0.dat.2"),payload=data.resolve("f0");
      byte[] beforeData=Files.readAllBytes(payload),beforeControl=Files.readAllBytes(map);
      cache.setStorageType(CacheFile.CT_PIECE_REORDER,false);cache.flushCache();
      if(cache.getStorageType()!=CacheFile.CT_PIECE_REORDER||cache.getLength()!=length||!Arrays.equals(beforeData,Files.readAllBytes(payload)))throw new AssertionError("X-to-R changes data/length");
      for(int i=0;i<offsets.length;i++)checkRead(cache,offsets[i],Math.min(16384,70000-offsets[i]),values[i]);
      Map before=BDecoder.decode(beforeControl),after=BDecoder.decode(Files.readAllBytes(map));
      if(!Arrays.equals((byte[])before.get("pieces"),(byte[])after.get("pieces"))||!Objects.equals(before.get("len"),after.get("len"))||!Objects.equals(before.get("next"),after.get("next")))throw new AssertionError("Reordered map identity changed");
      if(FilePriorityLoadOracle.unexpectedCalls!=0)throw new AssertionError("Unexpected dependency");
      Files.write(root.resolve("before-control.bin"),beforeControl);Files.write(root.resolve("original-reordered.bin"),beforeData);
      System.out.println("PASS_ORIGINAL_POPULATED_X_TO_R length="+length+" physical="+beforeData.length+" st="+before.get("st")+"->"+after.get("st"));
    }finally{file.getCacheFile().close();}
  }
}
