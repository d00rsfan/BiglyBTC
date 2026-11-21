import java.io.*;
import java.lang.reflect.*;
import java.nio.file.*;
import java.util.*;
import com.biglybt.core.disk.*;
import com.biglybt.core.disk.impl.*;
import com.biglybt.core.diskmanager.cache.CacheFileOwner;
import com.biglybt.core.download.*;
import com.biglybt.core.download.impl.DownloadManagerImpl;
import com.biglybt.core.torrent.*;
import com.biglybt.core.util.*;

public final class ActiveFileFacadeOracle {
  // No manager/controller constructor or Core startup. Only dependency getters
  // and notification are supplied;the actual facade decision/getters execute.
  public static final class Manager extends DownloadManagerImpl {
    TOTorrent torrentValue;DownloadManagerState stateValue;File saveValue;List<Long> priorities;int notifications;
    public Manager(){super(null,null,"","","",DownloadManager.STATE_STOPPED,false,false,false,false,null);throw new AssertionError("Fixture constructor must not run");}
    @Override public TOTorrent getTorrent(){return torrentValue;}
    @Override public DownloadManagerState getDownloadState(){return stateValue;}
    @Override public File getAbsoluteSaveLocation(){return saveValue;}
    @Override public Object getUserData(Object key){if(key.equals("file_priorities"))return priorities;return FilePriorityLoadOracle.unexpected("manager userdata "+key);}
    @Override public void setUserData(Object key,Object value){if(key.equals("file_priorities")&&value==null){priorities=null;return;}FilePriorityLoadOracle.unexpected("manager setuserdata");}
    @Override public boolean isDestroyed(){return true;}
    @Override public boolean isConstructed(){return false;}
    @Override protected void informPrioritiesChange(List<DiskManagerFileInfo> files){notifications++;}
  }
  static void set(Object object,String name,Object value)throws Exception{Field f=object.getClass().getDeclaredField(name);f.setAccessible(true);f.set(object,value);}
  static Object allocate(Class<?> type)throws Exception{Class<?> c=Class.forName("sun.misc.Unsafe");Field f=c.getDeclaredField("theUnsafe");f.setAccessible(true);return c.getMethod("allocateInstance",Class.class).invoke(f.get(null),type);}
  static void pinned(Class<?> type,Path expected)throws Exception{if(!Paths.get(type.getProtectionDomain().getCodeSource().getLocation().toURI()).toRealPath().equals(expected))throw new AssertionError("Unpinned "+type);}
  public static void main(String[] args)throws Exception{
    Path root=Paths.get(args[0]).toAbsolutePath();Files.createDirectories(root);Path expected=Paths.get(System.getProperty("oracle.expected.classes")).toRealPath();Class<?> controller=Class.forName("com.biglybt.core.download.impl.DownloadManagerController"),facade=Class.forName(controller.getName()+"$FileInfoFacadeSet");
    for(Class<?> c:new Class<?>[]{controller,facade,Class.forName(controller.getName()+"$fileInfoFacade"),DiskManagerUtil.class,DiskManagerFileInfoImpl.class,DiskManagerFileInfoSetImpl.class})pinned(c,expected);
    pinned(DownloadManagerImpl.class,Paths.get(System.getProperty("oracle.expected.manager.classes")).toRealPath());
    int rows=0,constructors=0;
    try(BufferedReader input=new BufferedReader(new InputStreamReader(System.in,"UTF-8"))){for(String line;(line=input.readLine())!=null;){
      String[] f=line.split("\t",-1);if(f.length!=4)throw new IllegalArgumentException("Fixture fields");int kind=Integer.parseInt(f[1]),phase=Integer.parseInt(f[3]);Path folder=root.resolve(f[0]),control=folder.resolve("control");Files.createDirectories(control);TOTorrent torrent=CompactOwnerOracle.torrent(new long[]{4096},0);String[] types={new String[]{"","L","C","R","X"}[kind]};Manager manager=(Manager)allocate(Manager.class);manager.torrentValue=torrent;manager.saveValue=folder.toFile();manager.priorities=f[2].equals("absent")?null:Arrays.asList(Long.valueOf(f[2]));final int[] stateFlags={0};
      DownloadManagerState state=FilePriorityLoadOracle.proxy(DownloadManagerState.class,(name,a)->{
        if(name.equals("getStateDir"))return control.toFile();if(name.equals("getFileLink"))return a[1];if(name.equals("getDownloadManager"))return manager;
        if(name.equals("getListAttribute")&&a[0].equals(DownloadManagerState.AT_FILE_STORE_TYPES))return types;
        if(name.equals("getLongAttribute")&&a[0].equals(DownloadManagerState.AT_FILE_ALLOC_STRATEGY))return 0L;
        if(name.equals("getAttribute")&&a[0].equals(DownloadManagerState.AT_INCOMP_FILE_SUFFIX))return null;
        if(name.equals("getMapAttribute")&&a[0].equals(DownloadManagerState.AT_FILE_DOWNLOADED))return null;
        if(name.equals("setLongParameter")&&a[0].equals(DownloadManagerState.PARAM_DND_FLAGS)){stateFlags[0]++;return null;}
        return FilePriorityLoadOracle.unexpected(name);
      });manager.stateValue=state;
      DiskManagerHelper disk=FilePriorityLoadOracle.proxy(DiskManagerHelper.class,(name,a)->{
        if(name.equals("getDownloadState"))return state;if(name.equals("getInternalName"))return f[0];if(name.equals("getCacheMode"))return CacheFileOwner.CACHE_MODE_NO_CACHE;if(name.equals("getTorrent"))return torrent;
        if(name.equals("skippedFileSetChanged")||name.equals("storageTypeChanged"))return null;
        return FilePriorityLoadOracle.unexpected(name);
      });
      DiskManagerFileInfoImpl file=new DiskManagerFileInfoImpl(disk,folder.toFile(),new StringInterner.FileKey("f0"),0,torrent.getFiles()[0],kind);constructors++;
      try{
        DiskManagerFileInfoSetImpl active=new DiskManagerFileInfoSetImpl(manager,new DiskManagerFileInfoImpl[]{file},disk){@Override public int[] getStorageTypes(){return new int[]{kind};}};
        Method load=DiskManagerUtil.class.getDeclaredMethod("loadFilePriorities",DownloadManager.class,DiskManagerFileInfoSet.class);load.setAccessible(true);load.invoke(null,manager,active);manager.priorities=f[2].equals("absent")?null:Arrays.asList(Long.valueOf(f[2]));
        final int[] liveGets={0};DiskManager live=FilePriorityLoadOracle.proxy(DiskManager.class,(name,a)->{if(name.equals("getState"))return phase;if(name.equals("getFileSet")){liveGets[0]++;return active;}return FilePriorityLoadOracle.unexpected(name);});
        Object owner=allocate(controller);set(owner,"download_manager",manager);set(owner,"download_manager_state",state);set(owner,"facade_mon",new AEMonitor("generated facade"));set(owner,"bInitialized",true);set(owner,"disk_manager_use_accessors",phase<0?null:live);
        Constructor<?> ctor=facade.getDeclaredConstructor(controller);ctor.setAccessible(true);set(owner,"fileFacadeSet",ctor.newInstance(owner));Method get=controller.getMethod("getDiskManagerFileInfoSet");get.setAccessible(true);DiskManagerFileInfoSet selected=(DiskManagerFileInfoSet)get.invoke(owner);DiskManagerFileInfo actual=selected.getFiles()[0];
        if(!actual.getClass().getName().equals(controller.getName()+"$fileInfoFacade")||FilePriorityLoadOracle.unexpectedCalls!=0||manager.notifications!=1)throw new AssertionError("Wrong actual facade/dependency boundary");
        boolean wanted=phase==DiskManager.CHECKING||phase==DiskManager.READY;if((liveGets[0]==1)!=wanted||stateFlags[0]!=(wanted?0:1))throw new AssertionError("Unexpected active/skeleton branch");
        System.out.println(f[0]+"\t"+kind+"\t"+phase+"\t"+(wanted?"active":"stopped")+"\t"+(actual.isSkipped()?1:0)+"\t"+actual.getPriority());rows++;
      }finally{file.getCacheFile().close();}
    }}
    if(com.biglybt.core.CoreFactory.isCoreAvailable())throw new AssertionError("Core unexpectedly started");System.err.println("PASS_ACTUAL_FILE_FACADE "+rows+" facade rows; "+constructors+" actual file constructors; no Core startup");
  }
}
