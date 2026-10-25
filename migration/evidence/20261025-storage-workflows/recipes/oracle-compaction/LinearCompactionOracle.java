import com.biglybt.core.Core;
import com.biglybt.core.disk.DiskManagerFileInfo;
import com.biglybt.core.download.DownloadManager;
import com.biglybt.core.download.DownloadManagerInitialisationAdapter;
import com.biglybt.core.torrent.TOTorrentFile;
import java.lang.reflect.Method;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.List;

/** Thin orchestration of the unchanged retained original and pinned helpers.
 * No storage codec, synthetic resume, or expected piece arithmetic lives here.
 */
public final class LinearCompactionOracle {
    private static void require(boolean okay, String message) {
        if (!okay) { throw new IllegalStateException(message); }
    }
    private static Object fixture(String name, Class<?>[] types, Object... values) throws Exception {
        Method method = StorageProfileOracle.class.getDeclaredMethod(name, types);
        method.setAccessible(true); // Reuse private test fixture methods, not original production internals.
        return method.invoke(null, values);
    }
    private static void run(String[] args) throws Exception {
        require(System.getSecurityManager() instanceof ReferenceFence, "Existing ReferenceFence required");
        require(args.length == 4, "LISTEN prepare-linear|original-compact|observe-linear|observe-compact EXPECTED_DONE KIND");
        Class.forName("com.biglybt.core.util.Debug"); // Proven pre-Core bootstrap ordering.
        final int listen = Integer.parseInt(args[0]), done = Integer.parseInt(args[2]), kind = Integer.parseInt(args[3]);
        final String mode = args[1];
        final boolean prepare = mode.equals("prepare-linear"), compact = mode.equals("original-compact");
        require(prepare || compact || mode.equals("observe-linear") || mode.equals("observe-compact"), "Unknown bounded mode");
        require(done >= 1 && done <= 8 && (kind == DiskManagerFileInfo.ST_LINEAR || kind == DiskManagerFileInfo.ST_COMPACT), "Invalid expected original state");
        require(!prepare || (kind == DiskManagerFileInfo.ST_LINEAR && done == 8), "Linear fixture starts fully verified");
        require(kind == ((compact || mode.equals("observe-compact")) ? DiskManagerFileInfo.ST_COMPACT : DiskManagerFileInfo.ST_LINEAR),
            "Mode and actual original storage kind disagree");
        Path root = Paths.get(System.getProperty("biglybtc.test.root")).toRealPath();
        Files.createDirectories(root.resolve("payload"));
        if (prepare) { fixture("generate", new Class<?>[]{Path.class}, root); }
        Core core = EngineOracle.configuredCore(listen); boolean started = false;
        try {
            core.start(); started = true;
            DownloadManager manager;
            if (prepare) {
                manager = core.getGlobalManager().addDownloadManager(root.resolve("input.torrent").toString(), null,
                    root.resolve("payload").toString(), DownloadManager.STATE_STOPPED, true, false,
                    new DownloadManagerInitialisationAdapter() {
                        public int getActions() { return ACT_NONE; }
                        public void initialised(DownloadManager download, boolean seeding) {
                            download.getDownloadState().setNetworks(new String[]{"Public"});
                            download.getDownloadState().setPeerSources(new String[]{"Plugin", "Incoming"});
                        }
                    });
                require(manager != null, "Original refused generated linear fixture");
                DiskManagerFileInfo[] files = manager.getDiskManagerFileInfoSet().getFiles();
                TOTorrentFile[] metadata = manager.getTorrent().getFiles();
                require(files.length == 3 && metadata.length == 3, "Bounded three-file original fixture required");
                for (int index = 0; index < files.length; ++index) {
                    require(files[index].setStorageType(DiskManagerFileInfo.ST_LINEAR), "Original refused linear storage");
                    files[index].setSkipped(false); files[index].setPriority(0);
                    Path actual = files[index].getFile(true).toPath().toAbsolutePath().normalize();
                    Path expected = root.resolve("expected/layout").resolve(metadata[index].getRelativePath()).normalize();
                    require(actual.startsWith(root.resolve("payload")) && !Files.isSymbolicLink(actual)
                        && expected.startsWith(root.resolve("expected/layout")), "Owned fixture path escaped");
                    Files.createDirectories(actual.getParent());
                    byte[] bytes = Files.readAllBytes(expected);
                    require(bytes.length == files[index].getLength(), "Actual fixture geometry differs");
                    Files.write(actual, bytes);
                }
                manager.getStats().setDownloadRateLimitBytesPerSecond(32768);
                manager.getStats().setUploadRateLimitBytesPerSecond(65536);
            } else {
                List<DownloadManager> saved = core.getGlobalManager().getDownloadManagers();
                require(saved.size() == 1, "Exactly one saved original download required"); manager = saved.get(0);
                require(manager.getState() == DownloadManager.STATE_STOPPED, "Original conversion/recheck requires stopped fixture");
            }
            if (compact) {
                DiskManagerFileInfo[] files = manager.getDiskManagerFileInfoSet().getFiles();
                require(files.length == 3 && files[1].getStorageType() == DiskManagerFileInfo.ST_LINEAR,
                    "Original reference must begin at actual linear storage");
                require(files[1].setStorageType(DiskManagerFileInfo.ST_COMPACT) && files[1].isSkipped(),
                    "Original stopped L-to-C/Skip conversion failed");
            }
            manager.initialize(); long deadline = System.nanoTime() + 90000000000L;
            while (manager.getState() != DownloadManager.STATE_READY) {
                require(manager.getState() != DownloadManager.STATE_ERROR && System.nanoTime() < deadline,
                    "Original READY failed: " + manager.getErrorDetails()); Thread.sleep(25);
            }
            DiskManagerFileInfo[] files = manager.getDiskManagerFileInfoSet().getFiles();
            require(files.length == 3 && files[1].getStorageType() == kind && files[1].isSkipped() == (kind == DiskManagerFileInfo.ST_COMPACT)
                && !files[0].isSkipped() && !files[2].isSkipped(), "Actual original storage/selection state differs");
            fixture("observe", new Class<?>[]{DownloadManager.class, Path.class, boolean.class, int.class}, manager, root, done == 8, done);
            manager.stopIt(DownloadManager.STATE_STOPPED, false, false);
        } finally { if (started) { core.stop(); } }
        System.out.println("PROFILE_ORIGINAL_STOPPED");
    }
    public static void main(String[] args) {
        try { run(args); System.exit(0); }
        catch (Throwable failure) { failure.printStackTrace(); System.exit(2); }
    }
}
