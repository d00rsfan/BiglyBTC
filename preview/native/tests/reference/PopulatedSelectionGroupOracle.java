import java.io.*;
import java.lang.reflect.*;
import java.nio.file.*;
import java.util.*;
import com.biglybt.core.disk.impl.*;
import com.biglybt.core.diskmanager.cache.*;
import com.biglybt.core.download.*;
import com.biglybt.core.torrent.*;
import com.biglybt.core.util.StringInterner;

/** Actual populated FileInfoSet unskip,with explicit no-Core/no-DND seams. */
public final class PopulatedSelectionGroupOracle {
  public static void main(String[] args)throws Exception {
    Path root=Paths.get(args[0]).toRealPath(),data=root.resolve("data"),control=root.resolve("control");
    Path expected=Paths.get(System.getProperty("oracle.expected.classes")).toRealPath();
    for(Class<?> type:new Class<?>[]{DiskManagerFileInfoImpl.class,DiskManagerFileInfoSetImpl.class,Class.forName("com.biglybt.core.diskmanager.file.impl.FMFileAccessController")})
      if(!Paths.get(type.getProtectionDomain().getCodeSource().getLocation().toURI()).toRealPath().equals(expected))throw new AssertionError("Unpinned actual class");
    TOTorrent torrent=TOTorrentFactory.deserialiseFromBEncodedByteArray(new TOTorrentFactory.TorrentDataHolder(Files.readAllBytes(root.resolve("state.dat"))));
    if(torrent.getFiles().length!=3)throw new AssertionError("Exact generated three-file fixture");
    final String[][] stored={new String[]{"X","C","L"}};List<String> calls=new ArrayList<>();final DownloadManager[] held={null};
    DownloadManagerState state=FilePriorityLoadOracle.proxy(DownloadManagerState.class,(method,a)->{
      if(method.equals("getStateDir"))return control.toFile();
      if(method.equals("getFileLink"))return a.length==2?a[1]:null;
      if(method.equals("getDownloadManager"))return held[0];
      if(method.equals("getLongAttribute")&&a[0].equals(DownloadManagerState.AT_FILE_ALLOC_STRATEGY))return 0L;
      if(method.equals("suppressStateSave"))return null;
      if(method.equals("setListAttribute")&&a[0].equals(DownloadManagerState.AT_FILE_STORE_TYPES)){stored[0]=((String[])a[1]).clone();return null;}
      if(method.equals("save")&&a.length==1&&a[0].equals(Boolean.FALSE))return null;
      return FilePriorityLoadOracle.unexpected(method);
    });
    DownloadManager manager=FilePriorityLoadOracle.proxy(DownloadManager.class,(method,a)->{
      if(method.equals("getDownloadState"))return state;if(method.equals("getTorrent"))return torrent;
      // Same declared seams as the qualified ordered-request matrix: physical
      // CacheFile conversion is real;DND and the later allocation owner are not.
      if(method.equals("isDestroyed"))return true;if(method.equals("isConstructed"))return false;
      return FilePriorityLoadOracle.unexpected(method);
    });held[0]=manager;
    DiskManagerHelper disk=FilePriorityLoadOracle.proxy(DiskManagerHelper.class,(method,a)->{
      if(method.equals("getDownloadState"))return state;if(method.equals("getInternalName"))return "Generated populated group";
      if(method.equals("getCacheMode"))return CacheFileOwner.CACHE_MODE_NO_CACHE;if(method.equals("getTorrent"))return torrent;
      if(method.equals("getStorageTypes"))return stored[0];
      if(method.equals("storageTypeChanged")||method.equals("skippedFileSetChanged"))return null;
      return FilePriorityLoadOracle.unexpected(method);
    });
    Field cache=DiskManagerFileInfoImpl.class.getDeclaredField("cache_file");cache.setAccessible(true);
    DiskManagerFileInfoImpl[] files=new DiskManagerFileInfoImpl[3];
    try{
      for(int index=0;index<files.length;index++){
        final int i=index;TOTorrentFile metadata=torrent.getFiles()[index];StringJoiner relative=new StringJoiner(File.separator);for(byte[] part:metadata.getPathComponents())relative.add(new String(part,"UTF-8"));
        files[index]=new DiskManagerFileInfoImpl(disk,data.toFile(),new StringInterner.FileKey(relative.toString()),index,metadata,new int[]{4,2,1}[index]);
        final CacheFile actual=files[index].getCacheFile();CacheFile observed=(CacheFile)Proxy.newProxyInstance(CacheFile.class.getClassLoader(),new Class<?>[]{CacheFile.class},(proxy,method,a)->{
          if(method.getName().equals("setStorageType"))calls.add(i+":"+a[0]);
          try{return method.invoke(actual,a);}catch(InvocationTargetException e){throw e.getCause();}
        });cache.set(files[index],observed);
      }
      DiskManagerFileInfoSetImpl set=new DiskManagerFileInfoSetImpl(manager,files,disk);set.setSkipped(new boolean[]{true,true,false},false);
      if(!calls.equals(Arrays.asList("1:1","0:3")))throw new AssertionError("Actual C-before-X calls "+calls);
      for(DiskManagerFileInfoImpl file:files)if(file.isSkipped())throw new AssertionError("Unacknowledged actual flags");
      if(!Arrays.equals(stored[0],new String[]{"R","L","L"})||FilePriorityLoadOracle.unexpectedCalls!=0)throw new AssertionError("Actual types/dependencies");
      System.out.println("PASS_ORIGINAL_POPULATED_SELECTION_GROUP calls=1:1,0:3 types=RLL skipped=000");
    }finally{for(DiskManagerFileInfoImpl file:files)if(file!=null)file.getCacheFile().close();}
  }
}
