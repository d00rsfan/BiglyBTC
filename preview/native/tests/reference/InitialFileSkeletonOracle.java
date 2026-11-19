import java.io.*;
import java.nio.file.*;
import java.util.*;
import com.biglybt.core.disk.*;
import com.biglybt.core.disk.impl.*;
import com.biglybt.core.download.*;
import com.biglybt.core.torrent.*;

public final class InitialFileSkeletonOracle {
  public static void main(String[] args) throws Exception {
    Path root=Paths.get(args[0]).toAbsolutePath();Files.createDirectories(root);
    Path expected=Paths.get(System.getProperty("oracle.expected.classes")).toRealPath();
    if(!Paths.get(DiskManagerUtil.class.getProtectionDomain().getCodeSource().getLocation().toURI()).toRealPath().equals(expected))throw new AssertionError("Unpinned Original factory");
    int rows=0;
    try(BufferedReader input=new BufferedReader(new InputStreamReader(System.in,"UTF-8"))){
      for(String line;(line=input.readLine())!=null;){
        String[] fields=line.split("\t",-1);if(fields.length!=6)throw new IllegalArgumentException("Row shape");
        long[] lengths={Long.parseLong(fields[1])};int kind=Integer.parseInt(fields[2]);
        if(lengths[0]!=4096||kind<1||kind>4)throw new IllegalArgumentException("Bounded fixture");
        TOTorrent torrent=CompactOwnerOracle.torrent(lengths,0);
        final List<Long> priorities=fields[3].equals("absent")?null:Arrays.asList(Long.valueOf(fields[3]));
        final String[] types={new String[]{"","L","C","R","X"}[kind]};
        DownloadManagerState state=FilePriorityLoadOracle.proxy(DownloadManagerState.class,(method,a)->{
          if(method.equals("getAttribute")&&a[0].equals(DownloadManagerState.AT_INCOMP_FILE_SUFFIX))return null;
          if(method.equals("getMapAttribute")&&a[0].equals(DownloadManagerState.AT_FILE_DOWNLOADED))return null;
          if(method.equals("getListAttribute")&&a[0].equals(DownloadManagerState.AT_FILE_STORE_TYPES))return types;
          return FilePriorityLoadOracle.unexpected(method);
        });
        DownloadManager manager=FilePriorityLoadOracle.proxy(DownloadManager.class,(method,a)->{
          if(method.equals("getTorrent"))return torrent;
          if(method.equals("getAbsoluteSaveLocation"))return root.resolve(fields[0]).toFile();
          if(method.equals("getDownloadState"))return state;
          if(method.equals("getUserData")&&a[0].equals("file_priorities"))return priorities;
          if(method.equals("setUserData")&&a[0].equals("file_priorities")&&a[1]==null)return null;
          return FilePriorityLoadOracle.unexpected(method);
        });
        DiskManagerListener listener=FilePriorityLoadOracle.proxy(DiskManagerListener.class,(method,a)->FilePriorityLoadOracle.unexpected(method));
        DiskManagerFileInfoSet set=DiskManagerUtil.getFileInfoSkeleton(manager,listener);
        DiskManagerFileInfo[] files=set.getFiles();
        if(files.length!=1||!files[0].getClass().getName().startsWith("com.biglybt.core.disk.impl.DiskManagerUtil$")||FilePriorityLoadOracle.unexpectedCalls!=0)throw new AssertionError("Actual skeleton/dependency boundary");
        System.out.println(fields[0]+"\t"+(files[0].isSkipped()?1:0)+":"+files[0].getPriority());rows++;
      }
    }
    System.err.println("PASS_ORIGINAL_INITIAL_SKELETON "+rows+" actual factory/load rows;no Core or cache owner");
  }
}
