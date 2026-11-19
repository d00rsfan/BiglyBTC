import java.io.*;
import java.lang.reflect.*;
import java.nio.file.*;
import java.util.*;
import com.biglybt.core.disk.DiskManagerFileInfo;
import com.biglybt.core.disk.impl.*;
import com.biglybt.core.disk.impl.resume.RDResumeHandler;
import com.biglybt.core.diskmanager.cache.CacheFileOwner;
import com.biglybt.core.download.*;
import com.biglybt.core.torrent.*;
import com.biglybt.core.util.*;

public final class CompactOwnerOracle {
  static TOTorrent torrent(long[] lengths, long padding) throws Exception {
    Map<String,Object> info = new HashMap<>();
    info.put("name", "Generated compact owner".getBytes("UTF-8"));
    info.put("piece length", 16384L);
    List<Object> list = new ArrayList<>(); long total = 0;
    for (int i = 0; i < lengths.length; i++) {
      Map<String,Object> file = new HashMap<>();
      file.put("length", lengths[i]); file.put("path", Arrays.asList(("f" + i).getBytes("UTF-8")));
      if ((padding & (1L << i)) != 0) file.put("attr", "p".getBytes("UTF-8"));
      list.add(file); total += lengths[i];
    }
    info.put("files", list); info.put("pieces", new byte[(int)((total + 16383) / 16384) * 20]);
    Map<String,Object> root = new HashMap<>(); root.put("info", info);
    root.put("announce", "http://generated.invalid/announce".getBytes("UTF-8"));
    return TOTorrentFactory.deserialiseFromBEncodedByteArray(new TOTorrentFactory.TorrentDataHolder(BEncoder.encode(root)));
  }
  public static void main(String[] args) throws Exception {
    Path root = Paths.get(args[0]).toAbsolutePath(); Files.createDirectories(root);
    Path expected = Paths.get(System.getProperty("oracle.expected.classes")).toRealPath();
    for (Class<?> type : new Class<?>[]{DiskManagerFileInfoImpl.class, DiskManagerFileInfoSetImpl.class, DiskManagerUtil.class, RDResumeHandler.class}) {
      if (!Paths.get(type.getProtectionDomain().getCodeSource().getLocation().toURI()).toRealPath().equals(expected)) throw new AssertionError("Unpinned class");
    }
    Method load = DiskManagerUtil.class.getDeclaredMethod("loadFilePriorities", DownloadManager.class, com.biglybt.core.disk.DiskManagerFileInfoSet.class);
    load.setAccessible(true);
    int rows = 0, constructors = 0, predicates = 0;
    try (BufferedReader input = new BufferedReader(new InputStreamReader(System.in, "UTF-8"))) {
      for (String line; (line = input.readLine()) != null;) {
        String[] f = line.split("\t", -1);
        if (f.length != 6) throw new IllegalArgumentException("Row shape");
        String[] sizes = f[1].split(","), kinds = f[2].split(",");
        int count = sizes.length, mode = Integer.parseInt(f[5]);
        if (count < 1 || count > 8 || kinds.length != count) throw new IllegalArgumentException("Bounded file census");
        long[] lengths = new long[count]; int[] types = new int[count]; String[] strings = new String[count];
        for (int i = 0; i < count; i++) { lengths[i] = Long.parseLong(sizes[i]); types[i] = Integer.parseInt(kinds[i]); strings[i] = new String[]{"", "L", "C", "R", "X"}[types[i]]; }
        TOTorrent torrent = torrent(lengths, Long.parseLong(f[4]));
        Path caseRoot = root.resolve("case-" + f[0]); Files.createDirectory(caseRoot);
        Path control = caseRoot.resolve("control"); Files.createDirectory(control);
        final List<Long> priorities;
        if (f[3].equals("absent")) priorities = null;
        else { priorities = new ArrayList<>(); if (!f[3].equals("empty")) for (String value : f[3].split(",")) priorities.add(Long.valueOf(value)); }
        final Map<String,Object> resume;
        if (mode == 0) resume = null;
        else { byte[] pieces = new byte[torrent.getNumberOfPieces()]; Arrays.fill(pieces, (byte)mode); Map<String,Object> data = new HashMap<>(); data.put("resume data", pieces); resume = new HashMap<>(); resume.put("data", data); }
        DownloadManagerState state = FilePriorityLoadOracle.proxy(DownloadManagerState.class, (method,a) -> {
          if (method.equals("getStateDir")) return control.toFile();
          if (method.equals("getFileLink")) return a[1];
          if (method.equals("getResumeData")) return resume;
          if (method.equals("getListAttribute") && a.length == 1 && a[0].equals(DownloadManagerState.AT_FILE_STORE_TYPES)) return strings;
          if (method.equals("getLongAttribute") && a[0].equals(DownloadManagerState.AT_FILE_ALLOC_STRATEGY)) return 0L;
          return FilePriorityLoadOracle.unexpected(method);
        });
        DownloadManager manager = FilePriorityLoadOracle.proxy(DownloadManager.class, (method,a) -> {
          if (method.equals("getDownloadState")) return state;
          if (method.equals("getTorrent")) return torrent;
          if (method.equals("getUserData") && a[0].equals("file_priorities")) return priorities;
          if (method.equals("setUserData") && a[0].equals("file_priorities") && a[1] == null) return null;
          return FilePriorityLoadOracle.unexpected(method);
        });
        DiskManagerHelper disk = FilePriorityLoadOracle.proxy(DiskManagerHelper.class, (method,a) -> {
          if (method.equals("getDownloadState")) return state;
          if (method.equals("getInternalName")) return caseRoot.getFileName().toString();
          if (method.equals("getCacheMode")) return CacheFileOwner.CACHE_MODE_NO_CACHE;
          if (method.equals("getTorrent")) return torrent;
          return FilePriorityLoadOracle.unexpected(method);
        });
        DiskManagerFileInfoImpl[] files = new DiskManagerFileInfoImpl[count];
        try {
          for (int i = 0; i < count; i++) { files[i] = new DiskManagerFileInfoImpl(disk, caseRoot.toFile(), new StringInterner.FileKey("f" + i), i, torrent.getFiles()[i], types[i]); constructors++; }
          // The inherited storage getter initializes DiskManagerImpl and requires
          // a complete Core singleton. Supply only the already-known explicit
          // requested kinds;keep real constructor,getFiles and load receiver.
          DiskManagerFileInfoSetImpl set = new DiskManagerFileInfoSetImpl(manager, files, disk) {
            @Override public int[] getStorageTypes() { return types.clone(); }
          };
          load.invoke(null, manager, set);
          StringBuilder values = new StringBuilder(), required = new StringBuilder();
          for (int i = 0; i < count; i++) {
            if (i != 0) { values.append(','); required.append(','); }
            values.append(files[i].isSkipped() ? 1 : 0).append(':').append(files[i].getPriority());
            if (types[i] == DiskManagerFileInfo.ST_COMPACT || types[i] == DiskManagerFileInfo.ST_REORDER_COMPACT) { required.append(RDResumeHandler.fileMustExist(manager, set, files[i]) ? '1' : '0'); predicates++; }
            else required.append('-');
          }
          if (FilePriorityLoadOracle.unexpectedCalls != 0) throw new AssertionError("Unexpected dependency");
          System.out.println(f[0] + "\t" + values + "\t" + required); rows++;
        } finally { for (DiskManagerFileInfoImpl file : files) if (file != null) file.getCacheFile().close(); }
      }
    }
    System.err.println("PASS_ORIGINAL_COMPACT_OWNER " + rows + " rows; " + constructors + " actual constructors; " + predicates + " actual predicates");
  }
}
