// SPDX-License-Identifier: GPL-2.0-or-later
// Test-only JDK17 fence. No application/Core is started by this reader oracle.
import java.io.File;
import java.security.Permission;
public final class HybridResumeFence extends SecurityManager {
    private final String root;
    public HybridResumeFence() throws Exception {
        root = new File(System.getProperty("biglybtc.test.root")).getCanonicalPath();
        if (!new File(root, "generated-fixtures.marker").isFile()) {
            throw new IllegalArgumentException("Generated fixture marker required");
        }
    }
    private void writable(String path) {
        try {
            String target = new File(path).getCanonicalPath();
            if (target.equals(root) || target.startsWith(root + File.separator)) { return; }
        } catch (Exception ignored) { }
        throw new SecurityException("Write outside generated oracle root");
    }
    @Override public void checkWrite(String path) { writable(path); }
    @Override public void checkDelete(String path) { writable(path); }
    @Override public void checkExec(String command) { throw new SecurityException("No child processes"); }
    @Override public void checkConnect(String host, int port) { throw new SecurityException("No network"); }
    @Override public void checkListen(int port) { throw new SecurityException("No listeners"); }
    @Override public void checkAccept(String host, int port) { throw new SecurityException("No peers"); }
    @Override public void checkPermission(Permission permission) {
        if (permission instanceof RuntimePermission && permission.getName().equals("setSecurityManager")) {
            throw new SecurityException("Reference fence cannot be removed");
        }
    }
}
