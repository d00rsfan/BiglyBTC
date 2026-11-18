import java.io.*;
import java.lang.reflect.*;
import java.nio.file.*;
import java.util.*;
import com.biglybt.core.disk.DiskManagerFileInfo;
import com.biglybt.core.disk.DiskManagerFileInfoSet;
import com.biglybt.core.disk.impl.DiskManagerUtil;
import com.biglybt.core.download.DownloadManager;

public final class FilePriorityLoadOracle {
  interface Call { Object apply(String method, Object[] arguments); }
  static int unexpectedCalls;
  static Object unexpected(String method) {
    unexpectedCalls++;
    throw new AssertionError("Unexpected dependency: " + method);
  }
  @SuppressWarnings("unchecked") static <T> T proxy(Class<T> kind, Call call) {
    return (T)Proxy.newProxyInstance(kind.getClassLoader(), new Class<?>[]{kind}, (p,m,a) -> {
      if (m.getDeclaringClass() == Object.class) {
        if (m.getName().equals("hashCode")) return System.identityHashCode(p);
        if (m.getName().equals("equals")) return p == a[0];
        return "GeneratedPriorityLoad";
      }
      return call.apply(m.getName(), a);
    });
  }
  public static void main(String[] args) throws Exception {
    Path expected = Paths.get(System.getProperty("oracle.expected.classes")).toRealPath();
    if (!Paths.get(DiskManagerUtil.class.getProtectionDomain().getCodeSource().getLocation().toURI()).toRealPath().equals(expected)) {
      throw new AssertionError("Unpinned original class origin");
    }
    Method load = DiskManagerUtil.class.getDeclaredMethod("loadFilePriorities", DownloadManager.class, DiskManagerFileInfoSet.class);
    load.setAccessible(true);
    int cases = 0;
    try (BufferedReader input = new BufferedReader(new InputStreamReader(System.in, "UTF-8"))) {
      for (String line; (line = input.readLine()) != null;) {
        String[] fields = line.split("\t", -1);
        if (fields.length != 3) throw new IllegalArgumentException("Row shape");
        int count = Integer.parseInt(fields[1]);
        if (count < 0 || count > 32) throw new IllegalArgumentException("Bounded files");
        final List<Object> saved;
        if (fields[2].equals("absent")) saved = null;
        else {
          saved = new ArrayList<>();
          if (!fields[2].equals("empty")) {
            for (String value : fields[2].split(",", -1)) {
              if (value.equals("null")) saved.add(null);
              else if (value.equals("bad")) saved.add("wrong original item type");
              else saved.add(Long.valueOf(value));
            }
          }
        }
        final int[] clears = {0}, replacements = {0};
        final int[][] priorities = {null};
        final boolean[][] skipped = {null};
        DiskManagerFileInfo[] files = new DiskManagerFileInfo[count];
        for (int i = 0; i < count; i++) files[i] = proxy(DiskManagerFileInfo.class, (m,a) -> unexpected(m));
        DownloadManager manager = proxy(DownloadManager.class, (method,a) -> {
          if (method.equals("getUserData") && a.length == 1 && a[0].equals("file_priorities")) return saved;
          if (method.equals("setUserData") && a.length == 2 && a[0].equals("file_priorities") && a[1] == null) { clears[0]++; return null; }
          return unexpected(method);
        });
        DiskManagerFileInfoSet set = proxy(DiskManagerFileInfoSet.class, (method,a) -> {
          if (method.equals("getFiles")) return files;
          if (method.equals("load") && a.length == 2 && a[0] instanceof int[] && a[1] instanceof boolean[]) {
            replacements[0]++; priorities[0] = ((int[])a[0]).clone(); skipped[0] = ((boolean[])a[1]).clone(); return null;
          }
          return unexpected(method);
        });
        load.invoke(null, manager, set);
        if (unexpectedCalls != 0 || clears[0] > 1 || replacements[0] > 1 || clears[0] + replacements[0] > 1) throw new AssertionError("Dependency/callback contract");
        StringBuilder values = new StringBuilder();
        if (replacements[0] != 0) {
          if (priorities[0].length != count || skipped[0].length != count) throw new AssertionError("Replacement shape");
          for (int i = 0; i < count; i++) { if (i != 0) values.append(','); values.append(skipped[0][i] ? 1 : 0).append(':').append(priorities[0][i]); }
        } else values.append('-');
        System.out.println(fields[0] + "\t" + replacements[0] + "\t" + clears[0] + "\t" + values);
        cases++;
      }
    }
    System.out.flush();
    System.err.println("PASS_ORIGINAL_FILE_PRIORITY_LOAD " + cases + " actual calls;no Core or filesystem activation");
  }
}
