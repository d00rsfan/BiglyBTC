import java.io.*;
import java.lang.reflect.*;
import java.nio.file.*;
import java.util.*;
import com.biglybt.core.disk.*;
import com.biglybt.core.disk.impl.*;
import com.biglybt.core.disk.impl.resume.RDResumeHandler;
import com.biglybt.core.diskmanager.cache.*;
import com.biglybt.core.download.*;
import com.biglybt.core.torrent.*;
import com.biglybt.core.util.*;

public final class PostSkipExistenceOracle {
  static String bits(boolean[] mask){StringBuilder out=new StringBuilder();for(boolean b:mask)out.append(b?'1':'0');return out.toString();}
  public static void main(String[] args)throws Exception {
    Path root=Paths.get(args[0]).toAbsolutePath();Files.createDirectories(root);Path expected=Paths.get(System.getProperty("oracle.expected.classes")).toRealPath();
    for(Class<?> type:new Class<?>[]{DiskManagerUtil.class,RDResumeHandler.class,DiskManagerFileInfoImpl.class,DiskManagerFileInfoSetImpl.class})
      if(!Paths.get(type.getProtectionDomain().getCodeSource().getLocation().toURI()).toRealPath().equals(expected))throw new AssertionError("Unpinned actual class");
    Path expectedDisk=Paths.get(System.getProperty("oracle.expected.disk.classes")).toRealPath();
    if(!Paths.get(DiskManagerImpl.class.getProtectionDomain().getCodeSource().getLocation().toURI()).toRealPath().equals(expectedDisk))throw new AssertionError("Wrong isolated disk-bootstrap class");
    int rows=0,constructors=0,allocations=0,deletions=0;
    try(BufferedReader input=new BufferedReader(new InputStreamReader(System.in,"UTF-8"))){for(String line;(line=input.readLine())!=null;){
      String[] f=line.split("\t",-1);if(f.length!=9)throw new IllegalArgumentException("Bounded row shape");String[] sizes=f[1].split(","),kinds=f[2].split(",");int count=sizes.length;
      if(count<1||count>8||kinds.length!=count)throw new IllegalArgumentException("Bounded census");long[] lengths=new long[count];int[] types=new int[count];String[] strings=new String[count];
      for(int i=0;i<count;i++){lengths[i]=Long.parseLong(sizes[i]);types[i]=Integer.parseInt(kinds[i]);strings[i]=new String[]{"","L","C","R","X"}[types[i]];}
      int skippedMask=Integer.parseInt(f[3]),changedMask=Integer.parseInt(f[4]),existsMask=Integer.parseInt(f[5]),resumeMode=Integer.parseInt(f[8]);boolean skipped=f[6].equals("1"),constructed=f[7].equals("1");if((!f[6].equals("0")&&!f[6].equals("1"))||(!f[7].equals("0")&&!f[7].equals("1")))throw new IllegalArgumentException("Exact Boolean fixture fields required");
      TOTorrent torrent=CompactOwnerOracle.torrent(lengths,0);Path folder=root.resolve("case-"+f[0]),control=folder.resolve("control");Files.createDirectories(control);
      final Map<String,Object> resume;if(resumeMode==0)resume=null;else{byte[] flags=new byte[torrent.getNumberOfPieces()];for(int i=0;i<flags.length;i++)flags[i]=(byte)(resumeMode==1?0:resumeMode==2?1:resumeMode==3?2:i%2==0?0:3);Map<String,Object> data=new HashMap<>();data.put("resume data",flags);resume=new HashMap<>();resume.put("data",data);}
      boolean[] activeConstructed={false},awaitTarget={false};boolean[] interesting=new boolean[count],existsCalls=new boolean[count],requested=new boolean[count];final DownloadManager[] held={null};
      DownloadManagerState state=FilePriorityLoadOracle.proxy(DownloadManagerState.class,(method,a)->{
        if(method.equals("getStateDir"))return control.toFile();if(method.equals("getFileLink"))return a.length==2?a[1]:null;
        if(method.equals("getResumeData"))return resume;if(method.equals("getListAttribute")&&a[0].equals(DownloadManagerState.AT_FILE_STORE_TYPES))return strings;
        if(method.equals("getDownloadManager"))return held[0];if(method.equals("getLongAttribute")&&a[0].equals(DownloadManagerState.AT_FILE_ALLOC_STRATEGY))return 0L;
        if(method.equals("getFlag"))return false;if(method.equals("suppressStateSave"))return null;
        return FilePriorityLoadOracle.unexpected(method);
      });
      DownloadManager manager=FilePriorityLoadOracle.proxy(DownloadManager.class,(method,a)->{
        if(method.equals("getDownloadState"))return state;if(method.equals("getTorrent"))return torrent;if(method.equals("isDestroyed"))return true;if(method.equals("isConstructed"))return activeConstructed[0];
        if(method.equals("requestAllocation")){for(Object value:(List)a[0])requested[((DiskManagerFileInfo)value).getIndex()]=true;return null;}
        return FilePriorityLoadOracle.unexpected(method);
      });held[0]=manager;
      DiskManagerHelper disk=FilePriorityLoadOracle.proxy(DiskManagerHelper.class,(method,a)->{
        if(method.equals("getDownloadState"))return state;if(method.equals("getInternalName"))return folder.getFileName().toString();if(method.equals("getCacheMode"))return CacheFileOwner.CACHE_MODE_NO_CACHE;if(method.equals("getTorrent"))return torrent;if(method.equals("getStorageTypes"))return strings;
        if(method.equals("skippedFileSetChanged")||method.equals("storageTypeChanged"))return null;
        return FilePriorityLoadOracle.unexpected(method);
      });
      DiskManagerFileInfoImpl[] actual=new DiskManagerFileInfoImpl[count];DiskManagerFileInfo[] observed=new DiskManagerFileInfo[count];
      try{
        for(int index=0;index<count;index++){final int i=index;actual[i]=new DiskManagerFileInfoImpl(disk,folder.toFile(),new StringInterner.FileKey("f"+i),i,torrent.getFiles()[i],types[i]);constructors++;
          observed[i]=(DiskManagerFileInfo)Proxy.newProxyInstance(DiskManagerFileInfo.class.getClassLoader(),new Class<?>[]{DiskManagerFileInfo.class},(proxy,method,a)->{
            if(method.getName().equals("getFirstPieceNumber")&&awaitTarget[0]){interesting[i]=true;awaitTarget[0]=false;}if(method.getName().equals("exists"))existsCalls[i]=true;
            try{return method.invoke(actual[i],a);}catch(InvocationTargetException e){throw e.getCause();}
          });
        }
        // Establish current filesystem facts AFTER construction: Source's
        // separate recoverConfig path crashes for an existing zero-size R/X.
        // The tested constructed post-skip phase is unchanged;assert actual
        // FileInfo.exists observes every supplied current fact before entry.
        for(int i=0;i<count;i++){if((existsMask&(1<<i))!=0)Files.write(folder.resolve("f"+i),new byte[0]);if(actual[i].exists()!=((existsMask&(1<<i))!=0))throw new AssertionError("Wrong current existence fact");}
        // Only known requested-kind getter seam;real constructor/load receiver.
        DiskManagerFileInfoSetImpl loaded=new DiskManagerFileInfoSetImpl(manager,actual,disk){@Override public int[] getStorageTypes(){return types.clone();}};
        boolean[] skips=new boolean[count],changed=new boolean[count];for(int i=0;i<count;i++){skips[i]=(skippedMask&(1<<i))!=0;changed[i]=(changedMask&(1<<i))!=0;}loaded.load(new int[count],skips);
        DiskManagerFileInfoSet set=FilePriorityLoadOracle.proxy(DiskManagerFileInfoSet.class,(method,a)->{
          if(method.equals("getFiles"))return observed;if(method.equals("getStorageTypes")){awaitTarget[0]=true;return types.clone();}return FilePriorityLoadOracle.unexpected(method);
        });
        activeConstructed[0]=constructed;DiskManagerUtil.doFileExistenceChecksAfterSkipChange(set,changed,skipped,manager);
        boolean[] removed=new boolean[count];for(int i=0;i<count;i++){removed[i]=(existsMask&(1<<i))!=0&&!Files.exists(folder.resolve("f"+i));if(removed[i])deletions++;if(requested[i])allocations++;}
        if(FilePriorityLoadOracle.unexpectedCalls!=0)throw new AssertionError("Unexpected dependency");System.out.println(f[0]+"\t"+bits(interesting)+"\t"+bits(existsCalls)+"\t"+bits(requested)+"\t"+bits(removed));rows++;
      }finally{for(DiskManagerFileInfoImpl file:actual)if(file!=null)file.getCacheFile().close();}
    }}
    if(com.biglybt.core.CoreFactory.isCoreAvailable())throw new AssertionError("Original Core was unexpectedly created");
    System.err.println("PASS_ORIGINAL_POST_SKIP_EXISTENCE "+rows+" rows; "+constructors+" actual constructors; "+allocations+" requested slots; "+deletions+" actual empty-file deletions");
  }
}
