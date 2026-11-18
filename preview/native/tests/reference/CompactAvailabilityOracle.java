import java.io.*;
import java.lang.reflect.*;
import java.nio.file.*;
import java.util.*;
import com.biglybt.core.disk.DiskManagerFileInfo;
import com.biglybt.core.disk.DiskManagerFileInfoSet;
import com.biglybt.core.disk.impl.resume.RDResumeHandler;
import com.biglybt.core.download.DownloadManager;
import com.biglybt.core.download.DownloadManagerState;
import com.biglybt.core.torrent.*;
import com.biglybt.core.util.BEncoder;

public final class CompactAvailabilityOracle {
  interface Call {Object apply(String name)throws Throwable;}
  @SuppressWarnings("unchecked") static <T>T proxy(Class<T> kind,Call call){
    return (T)Proxy.newProxyInstance(kind.getClassLoader(),new Class<?>[]{kind},(p,m,a)->{
      if(m.getDeclaringClass()==Object.class){
        if(m.getName().equals("hashCode"))return System.identityHashCode(p);
        if(m.getName().equals("equals"))return p==a[0];
        return "GeneratedOracle";
      }
      return call.apply(m.getName());
    });
  }
  static Object unexpected(String method){throw new IllegalStateException("Unexpected original dependency: "+method);}
  static TOTorrent torrent(long[] lengths)throws Exception{
    Map<String,Object> info=new HashMap<>();info.put("name","Generated compact policy".getBytes("UTF-8"));info.put("piece length",16384L);
    List<Object> files=new ArrayList<>();long total=0;
    for(int i=0;i<lengths.length;i++){Map<String,Object> file=new HashMap<>();file.put("length",lengths[i]);file.put("path",Arrays.asList(("f"+i).getBytes("UTF-8")));files.add(file);total+=lengths[i];}
    info.put("files",files);info.put("pieces",new byte[(int)((total+16383)/16384)*20]);
    Map<String,Object> root=new HashMap<>();root.put("info",info);root.put("announce","http://generated.invalid/announce".getBytes("UTF-8"));
    return TOTorrentFactory.deserialiseFromBEncodedByteArray(new TOTorrentFactory.TorrentDataHolder(BEncoder.encode(root)));
  }
  public static void main(String[] args)throws Exception{
    Path expected=Paths.get(System.getProperty("oracle.expected.classes")).toRealPath();
    if(!Paths.get(RDResumeHandler.class.getProtectionDomain().getCodeSource().getLocation().toURI()).toRealPath().equals(expected))throw new IllegalStateException("Unpinned original class origin");
    String previous="";TOTorrent original=null;int cases=0;
    try(BufferedReader input=new BufferedReader(new InputStreamReader(System.in,"UTF-8"))){
      for(String line;(line=input.readLine())!=null;){
        String[] fields=line.split("\\t");if(fields.length!=6)throw new IllegalArgumentException("Bounded row shape");
        String key=fields[1];if(!key.equals(previous)){String[] parts=key.split(",");long[] lengths=new long[parts.length];for(int i=0;i<parts.length;i++)lengths[i]=Long.parseLong(parts[i]);original=torrent(lengths);previous=key;}
        final TOTorrent selected=original;final TOTorrentFile[] metadata=selected.getFiles();
        int n=metadata.length,target=Integer.parseInt(fields[2]),compact=Integer.parseInt(fields[4]),mode=Integer.parseInt(fields[5]);long mask=Long.parseLong(fields[3]);
        if(n<1||n>16||target<0||target>=n||(compact!=DiskManagerFileInfo.ST_COMPACT&&compact!=DiskManagerFileInfo.ST_REORDER_COMPACT))throw new IllegalArgumentException("Bounded target/kind");
        DiskManagerFileInfo[] files=new DiskManagerFileInfo[n];int[] kinds=new int[n];StringBuilder geometry=new StringBuilder();
        for(int i=0;i<n;i++){
          final int index=i;final boolean skipped=(mask&(1L<<i))!=0;final TOTorrentFile file=metadata[i];
          files[i]=proxy(DiskManagerFileInfo.class,name->{switch(name){case "getIndex":return index;case "getFirstPieceNumber":return file.getFirstPieceNumber();case "getLastPieceNumber":return file.getLastPieceNumber();case "isSkipped":return skipped;default:return unexpected(name);}});
          kinds[i]=i==target?compact:(i%2==0?DiskManagerFileInfo.ST_LINEAR:DiskManagerFileInfo.ST_REORDER);
          if(i>0)geometry.append(',');geometry.append(file.getFirstPieceNumber()).append(':').append(file.getLastPieceNumber()).append(':').append(skipped?1:0);
        }
        Map<String,Object> resume=null;
        if(mode!=0){Map<String,Object> data=new HashMap<>();byte[] pieces=new byte[selected.getNumberOfPieces()];Arrays.fill(pieces,(byte)mode);data.put("resume data",pieces);resume=new HashMap<>();resume.put("data",data);}
        final Map<String,Object> heldResume=resume;
        DownloadManagerState state=proxy(DownloadManagerState.class,name->name.equals("getResumeData")?heldResume:unexpected(name));
        DownloadManager download=proxy(DownloadManager.class,name->name.equals("getDownloadState")?state:unexpected(name));
        DiskManagerFileInfoSet set=proxy(DiskManagerFileInfoSet.class,name->name.equals("getFiles")?files:name.equals("getStorageTypes")?kinds:unexpected(name));
        boolean required=RDResumeHandler.fileMustExist(download,set,files[target]);
        System.out.println(fields[0]+"\t"+target+"\t"+geometry+"\t"+(required?1:0));cases++;
      }
    }
    System.err.println("PASS_ORIGINAL_COMPACT_AVAILABILITY "+cases+" actual RDResumeHandler calls;actual TOTorrentFile geometry;no Core or transport activation");
  }
}
