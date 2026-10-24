import java.io.File;
import java.net.InetAddress;
import java.security.Permission;

/** New probe fence: class/JDK reads only; no config/profile IO, threads or networking. */
public final class QueueOracleFence extends SecurityManager {
    private final String classes, cohort, jdk;
    private boolean violated;
    public QueueOracleFence() throws Exception {
        String owned = new File(System.getProperty("queue.oracle.root")).getCanonicalPath();
        if (!owned.startsWith("/tmp/biglybtc-queue-oracle-")) { throw new IllegalArgumentException("Generated root required"); }
        classes = owned + "/classes";
        cohort = new File(System.getProperty("queue.oracle.cohort")).getCanonicalPath();
        jdk = new File(System.getProperty("java.home")).getCanonicalPath();
        if (!cohort.equals("/tmp/biglybtc-oracle/classes")) { throw new IllegalArgumentException("Retained cohort required"); }
    }
    private SecurityException denied(String message) { violated = true; return new SecurityException(message); }
    private static boolean under(String value, String root) { return value.equals(root) || value.startsWith(root + File.separator); }
    @Override public void checkRead(String value) {
        try {
            String path = new File(value).getCanonicalPath();
            if (!under(path, classes) && !under(path, cohort) && !under(path, jdk)
                    && !under(path, "/usr/share/java") && !under(path, "/etc/java-17-openjdk")) {
                throw denied("Read outside exact class/JDK roots: " + path);
            }
        } catch (java.io.IOException error) { throw denied("Unresolvable read"); }
    }
    @Override public void checkRead(String value, Object context) { checkRead(value); }
    @Override public void checkWrite(String path) { throw denied("No writes"); }
    @Override public void checkDelete(String path) { throw denied("No deletes"); }
    @Override public void checkExec(String value) { throw denied("No subprocesses"); }
    @Override public void checkConnect(String host, int port) { throw denied("No connections/DNS"); }
    @Override public void checkConnect(String host, int port, Object context) { checkConnect(host, port); }
    @Override public void checkListen(int port) { throw denied("No listeners"); }
    @Override public void checkAccept(String host, int port) { throw denied("No accepts"); }
    @Override public void checkMulticast(InetAddress value) { throw denied("No multicast"); }
    @Override public void checkAccess(Thread thread) {
        if (thread != Thread.currentThread()) { throw denied("No other thread access/creation"); }
    }
    @Override public void checkAccess(ThreadGroup group) { throw denied("No thread creation"); }
    @Override public void checkLink(String value) {
        if (!value.equals("zip") && !value.equals("nio")) { throw denied("No application/native networking library"); }
    }
    @Override public void checkPermission(Permission permission) {
        if (permission instanceof RuntimePermission && permission.getName().equals("setSecurityManager")) {
            throw denied("Fence replacement denied");
        }
        if (permission instanceof java.nio.file.LinkPermission) { throw denied("No links"); }
        if (permission instanceof java.io.FilePermission) {
            String actions = permission.getActions();
            if (actions.contains("write") || actions.contains("delete") || actions.contains("execute")) {
                throw denied("No filesystem mutation/execution");
            }
        }
    }
    @Override public void checkPermission(Permission permission, Object context) { checkPermission(permission); }
    public void verify() { if (violated) { throw new AssertionError("Latched forbidden fence operation"); } }
}
