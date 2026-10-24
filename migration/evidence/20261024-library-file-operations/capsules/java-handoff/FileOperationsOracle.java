import com.biglybt.core.Core;
import com.biglybt.core.disk.DiskManagerFileInfo;
import com.biglybt.core.disk.DiskManagerPiece;
import com.biglybt.core.download.DownloadManager;
import com.biglybt.core.torrent.TOTorrent;
import com.biglybt.core.util.BEncoder;
import com.biglybt.core.util.ByteFormatter;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.security.MessageDigest;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/** Existing original GlobalManager/READY readback, bounded generated profiles.
 * EngineOracle is reused unchanged; no original class is replaced or rebuilt.
 */
public final class FileOperationsOracle {
    private static void require(boolean value, String reason) {
        if (!value) { throw new IllegalStateException(reason); }
    }
    private static void guard() {
        require(System.getSecurityManager() instanceof ReferenceFence, "Owned reference fence required");
    }
    private static String hex(byte[] value) { return ByteFormatter.encodeString(value); }
    private static void emitValue(String hash, String scope, String key, Object value) throws Exception {
        if (value == null) { return; }
        Map<String,Object> wrapper = new HashMap<String,Object>(); wrapper.put("value", value);
        System.out.println("VALUE\t" + hash + "\t" + scope + "\t"
            + hex(key.getBytes(StandardCharsets.UTF_8)) + "\t" + hex(BEncoder.encode(wrapper)));
    }
    private static void run(String[] args) throws Exception {
        guard(); require(args.length == 1, "Owned listener port required");
        int port = Integer.parseInt(args[0]); require(port > 0 && port <= 65535, "Listener range");
        Path root = Paths.get(System.getProperty("biglybtc.test.root")).toRealPath();
        Core core = EngineOracle.configuredCore(port);
        boolean started = false;
        try {
            core.start(); started = true; guard();
            List<DownloadManager> rows = core.getGlobalManager().getDownloadManagers();
            require(rows.size() >= 1 && rows.size() <= 2, "Expected only selected torrent and survivor");
            System.out.println("ROWS\t" + rows.size());
            for (DownloadManager manager : rows) {
                String hash = hex(manager.getTorrent().getHash());
                System.out.println("ROW\t" + hash + "\t" + manager.getState() + "\t" + manager.isForceStart()
                    + "\t" + manager.isPaused() + "\t" + manager.getPosition()
                    + "\t" + manager.getStats().getTotalDataBytesReceived()
                    + "\t" + manager.getStats().getTotalDataBytesSent());
                TOTorrent state = manager.getDownloadState().getTorrent();
                Map attributes = state.getAdditionalMapProperty("attributes");
                if (attributes != null) {
                    emitValue(hash, "attributes", "opaque.state", attributes.get("opaque.state"));
                    emitValue(hash, "attributes", "opaque.cache", attributes.get("opaque.cache"));
                }
                emitValue(hash, "root", "unknown.future.plugin", state.getAdditionalMapProperty("unknown.future.plugin"));
                manager.initialize();
                long deadline = System.nanoTime() + 90000000000L;
                while (manager.getState() != DownloadManager.STATE_READY) {
                    guard();
                    require(manager.getState() != DownloadManager.STATE_ERROR, manager.getErrorDetails());
                    require(System.nanoTime() < deadline, "Original READY recheck deadline expired");
                    Thread.sleep(25);
                }
                int checked = 0;
                DiskManagerPiece[] pieces = manager.getDiskManager().getPieces();
                for (DiskManagerPiece piece : pieces) { if (piece.isDone()) { ++checked; } }
                System.out.println("PIECES\t" + hash + "\t" + checked + "\t" + pieces.length);
                DiskManagerFileInfo[] files = manager.getDiskManagerFileInfoSet().getFiles();
                require(files.length >= 1 && files.length <= 8, "Bounded ordinary fixture file count");
                for (DiskManagerFileInfo file : files) {
                    require(!file.getTorrentFile().isPadFile(), "No physical synthetic padding in these v1 fixtures");
                    Path path = file.getFile(true).toPath().toAbsolutePath().normalize();
                    require(!Files.isSymbolicLink(path) && path.toRealPath().startsWith(root), "File mapping outside owned fixture");
                    require(Files.size(path) <= 1048576, "Generated fixture payload bound");
                    byte[] bytes = Files.readAllBytes(path);
                    String digest = hex(MessageDigest.getInstance("SHA-256").digest(bytes));
                    System.out.println("FILE\t" + hash + "\t" + file.getIndex() + "\t" + file.getPriority()
                        + "\t" + file.isSkipped() + "\t" + file.getDownloaded() + "\t" + file.getLength()
                        + "\t" + hex(path.toString().getBytes(StandardCharsets.UTF_8)) + "\t" + digest);
                }
                // Actual READY check only; never startDownload/addPeer/force/requeue.
            }
        } finally {
            if (started) { core.stop(); }
        }
        guard(); System.out.println("FILE_OPERATIONS_ORIGINAL_READBACK_PASS");
    }
    public static void main(String[] args) {
        try { run(args); System.exit(0); }
        catch (Throwable error) { error.printStackTrace(); System.exit(2); }
    }
}
