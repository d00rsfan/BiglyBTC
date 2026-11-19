import java.io.*;
import java.lang.reflect.*;
import java.nio.file.*;
import java.util.*;
import com.biglybt.core.disk.impl.*;
import com.biglybt.core.diskmanager.cache.*;
import com.biglybt.core.download.*;
import com.biglybt.core.torrent.*;
import com.biglybt.core.util.StringInterner;

public final class UnskipStorageOracle {
  static String bits(boolean[] values) { StringBuilder b=new StringBuilder(); for(boolean v:values)b.append(v?'1':'0');return b.toString(); }
  static String types(String[] values) { return String.join("",values); }
  public static void main(String[] args) throws Exception {
    Path root=Paths.get(args[0]).toAbsolutePath();Files.createDirectories(root);
    Path expected=Paths.get(System.getProperty("oracle.expected.classes")).toRealPath();
    for(Class<?> type:new Class<?>[]{DiskManagerFileInfoImpl.class,DiskManagerFileInfoSetImpl.class})
      if(!Paths.get(type.getProtectionDomain().getCodeSource().getLocation().toURI()).toRealPath().equals(expected))throw new AssertionError("Unpinned class");
    Field cache=DiskManagerFileInfoImpl.class.getDeclaredField("cache_file");cache.setAccessible(true);
    int rows=0,constructors=0,requests=0,cacheCalls=0;
    try(BufferedReader input=new BufferedReader(new InputStreamReader(System.in,"UTF-8"))){
      for(String line;(line=input.readLine())!=null;){
        String[] f=line.split("\t",-1);if(f.length!=6)throw new IllegalArgumentException("Bounded row");
        String[] k=f[1].split(",");int count=k.length,change=Integer.parseInt(f[2]),failure=Integer.parseInt(f[4]);boolean skipped=f[3].equals("1");int initial=Integer.parseInt(f[5]);
        if(count<1||count>8)throw new IllegalArgumentException("Bounded count");
        String[] stores=new String[count];int[] requested=new int[count];long[] lengths=new long[count];Arrays.fill(lengths,32769L);
        for(int i=0;i<count;i++){requested[i]=Integer.parseInt(k[i]);stores[i]=new String[]{"","L","C","R","X"}[requested[i]];}
        final String[][] stored={stores};final DownloadManager[] heldManager={null};
        Path caseRoot=root.resolve("case-"+f[0]);Files.createDirectory(caseRoot);Path control=caseRoot.resolve("control");Files.createDirectory(control);
        TOTorrent torrent=CompactOwnerOracle.torrent(lengths,0);
        List<String> events=new ArrayList<>();final int[] calls={0};
        DownloadManagerState state=FilePriorityLoadOracle.proxy(DownloadManagerState.class,(method,a)->{
          if(method.equals("getStateDir"))return control.toFile();
          if(method.equals("getFileLink"))return a.length==2?a[1]:null;
          if(method.equals("getDownloadManager"))return heldManager[0];
          if(method.equals("getLongAttribute")&&a[0].equals(DownloadManagerState.AT_FILE_ALLOC_STRATEGY))return 0L;
          if(method.equals("suppressStateSave")){events.add((Boolean)a[0]?"suppress1":"suppress0");return null;}
          if(method.equals("setListAttribute")&&a[0].equals(DownloadManagerState.AT_FILE_STORE_TYPES)){stored[0]=((String[])a[1]).clone();events.add("types:"+types(stored[0]));return null;}
          if(method.equals("save")&&a.length==1&&a[0].equals(Boolean.FALSE)){events.add("save0");return null;}
          return FilePriorityLoadOracle.unexpected(method);
        });
        DownloadManager manager=FilePriorityLoadOracle.proxy(DownloadManager.class,(method,a)->{
          if(method.equals("getDownloadState"))return state;
          if(method.equals("getTorrent"))return torrent;
          // Boundaries deliberately suppress DND moves and the later existence/
          // allocation owner;this probe qualifies conversion/flag ordering only.
          if(method.equals("isDestroyed"))return true;
          if(method.equals("isConstructed")){events.add("post-construction0");return false;}
          return FilePriorityLoadOracle.unexpected(method);
        });heldManager[0]=manager;
        DiskManagerHelper disk=FilePriorityLoadOracle.proxy(DiskManagerHelper.class,(method,a)->{
          if(method.equals("getDownloadState"))return state;
          if(method.equals("getInternalName"))return caseRoot.getFileName().toString();
          if(method.equals("getCacheMode"))return CacheFileOwner.CACHE_MODE_NO_CACHE;
          if(method.equals("getTorrent"))return torrent;
          if(method.equals("getStorageTypes"))return stored[0];
          if(method.equals("storageTypeChanged")){events.add("storage:"+((DiskManagerFileInfoImpl)a[0]).getIndex());return null;}
          if(method.equals("skippedFileSetChanged")){DiskManagerFileInfoImpl file=(DiskManagerFileInfoImpl)a[0];events.add("skip:"+file.getIndex()+":"+(file.isSkipped()?1:0));return null;}
          if(method.equals("setFailedAndRecheck")){events.add("failed:"+((DiskManagerFileInfoImpl)a[0]).getIndex());return null;}
          return FilePriorityLoadOracle.unexpected(method);
        });
        DiskManagerFileInfoImpl[] files=new DiskManagerFileInfoImpl[count];
        try{
          for(int i=0;i<count;i++){
            Files.write(caseRoot.resolve("f"+i),new byte[0]);
            files[i]=new DiskManagerFileInfoImpl(disk,caseRoot.toFile(),new StringInterner.FileKey("f"+i),i,torrent.getFiles()[i],requested[i]);constructors++;
            final CacheFile actual=files[i].getCacheFile();final int index=i;
            CacheFile observed=(CacheFile)Proxy.newProxyInstance(CacheFile.class.getClassLoader(),new Class<?>[]{CacheFile.class},(p,m,a)->{
              if(m.getName().equals("setStorageType")){
                calls[0]++;events.add("cache:"+index+":"+a[0]);
                if(failure!=0&&calls[0]==failure)throw new IllegalStateException("Generated conversion failure");
              }
              try{return m.invoke(actual,a);}catch(InvocationTargetException e){throw e.getCause();}
            });cache.set(files[i],observed);
          }
          DiskManagerFileInfoSetImpl set=new DiskManagerFileInfoSetImpl(manager,files,disk){
            @Override public boolean[] setStorageTypes(boolean[] selected,int target,boolean force){
              events.add("request:"+target+":"+bits(selected));
              boolean[] result=super.setStorageTypes(selected,target,force);
              events.add("result:"+target+":"+bits(result));return result;
            }
          };
          if(initial>=0){boolean[] states=new boolean[count];for(int i=0;i<count;i++)states[i]=(initial&(1<<i))!=0;set.load(new int[count],states);}
          events.clear();boolean[] selected=new boolean[count];for(int i=0;i<count;i++)selected[i]=(change&(1<<i))!=0;
          set.setSkipped(selected,skipped);
          if(FilePriorityLoadOracle.unexpectedCalls!=0)throw new AssertionError("Unexpected dependency");
          boolean[] finalSkip=new boolean[count];StringBuilder physical=new StringBuilder();
          for(int i=0;i<count;i++){finalSkip[i]=files[i].isSkipped();if(i!=0)physical.append(',');physical.append(files[i].getCacheFile().getStorageType());}
          for(String event:events)if(event.startsWith("request:"))requests++;
          cacheCalls+=calls[0];System.out.println(f[0]+"\t"+bits(finalSkip)+"\t"+types(stored[0])+"\t"+physical+"\t"+String.join(";",events));rows++;
        }finally{for(DiskManagerFileInfoImpl file:files)if(file!=null)file.getCacheFile().close();}
      }
    }
    System.err.println("PASS_ORIGINAL_UNSKIP_STORAGE "+rows+" rows; "+constructors+" actual constructors; "+requests+" actual conversion requests; "+cacheCalls+" cache calls");
  }
}
