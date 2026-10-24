import com.biglybt.core.Core;
import com.biglybt.core.disk.DiskManagerFileInfo;
import com.biglybt.core.disk.DiskManagerPiece;
import com.biglybt.core.download.DownloadManager;
import com.biglybt.core.torrent.TOTorrentFile;
import com.biglybt.core.util.ByteFormatter;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Arrays;
import java.util.List;

/** Real original GlobalManager/DiskManager reload of an owned managed checkpoint.
 * Reuses the pinned EngineOracle configuration; no decoding proxy or UI startup.
 */
public final class ManagedHandoffOracle {
    private static void guard() {
        if (!(System.getSecurityManager() instanceof ReferenceFence)) {
            throw new IllegalStateException("The existing owned-reference guard is required");
        }
    }
    private static void run(String[] args) throws Exception {
        guard();
        if (args.length != 4) { throw new IllegalArgumentException("listen, checked pieces, expected source, partial|complete"); }
        int listen = Integer.parseInt(args[0]);
        int expectedPieces = Integer.parseInt(args[1]);
        Path expected = Paths.get(args[2]).toAbsolutePath().normalize();
        boolean complete = args[3].equals("complete");
        if ((!complete && !args[3].equals("partial")) || expectedPieces < 1 || expectedPieces > 8) {
            throw new IllegalArgumentException("Invalid bounded handoff observation");
        }
        Path ownedPayload = Paths.get(System.getProperty("biglybtc.test.root")).resolve("payload").toRealPath();
        Core core = EngineOracle.configuredCore(listen);
        boolean started = false;
        boolean checked = false;
        try {
            core.start(); started = true; guard();
            List<DownloadManager> saved = core.getGlobalManager().getDownloadManagers();
            if (saved.size() != 1) { throw new IllegalStateException("Exactly one existing saved download required"); }
            DownloadManager manager = saved.get(0);
            System.out.println("HANDOFF_HASH\t" + ByteFormatter.encodeString(manager.getTorrent().getHash()));
            System.out.println("HANDOFF_INTENT\t" + manager.getState() + "\t" + manager.isForceStart()
                + "\t" + manager.isPaused() + "\t" + manager.getPosition()
                + "\t" + manager.getStats().getDownloadRateLimitBytesPerSecond()
                + "\t" + manager.getStats().getUploadRateLimitBytesPerSecond());
            manager.initialize();
            long deadline = System.nanoTime() + 90000000000L;
            while (System.nanoTime() < deadline) {
                guard();
                if (manager.getState() == DownloadManager.STATE_ERROR) {
                    throw new IllegalStateException(manager.getErrorDetails());
                }
                if (manager.getState() == DownloadManager.STATE_READY) {
                    DiskManagerPiece[] pieces = manager.getDiskManager().getPieces();
                    int done = 0;
                    for (DiskManagerPiece piece : pieces) { if (piece.isDone()) { ++done; } }
                    if (pieces.length != 8 || done != expectedPieces || (complete && done != pieces.length)
                            || (!complete && done == pieces.length)) {
                        throw new IllegalStateException("Actual Java checked piece count differs: " + done + "/" + pieces.length);
                    }
                    System.out.println("HANDOFF_READY\t" + done + "\t" + pieces.length);
                    System.out.println("HANDOFF_COUNTERS\t" + manager.getStats().getTotalDataBytesReceived()
                        + "\t" + manager.getStats().getTotalDataBytesSent());
                    TOTorrentFile[] metadata = manager.getTorrent().getFiles();
                    for (DiskManagerFileInfo file : manager.getDiskManagerFileInfoSet().getFiles()) {
                        TOTorrentFile source = metadata[file.getIndex()];
                        Path actual = file.getFile(true).toPath().toAbsolutePath().normalize();
                        if (source.isPadFile()) { throw new IllegalStateException("This v1 handoff does not fabricate padding files"); }
                        if (Files.isSymbolicLink(actual) || !actual.toRealPath().startsWith(ownedPayload)) {
                            throw new IllegalStateException("Mapped file is outside the owned payload");
                        }
                        if (complete) {
                            Path wanted = metadata.length == 1 ? expected : expected.resolve(source.getRelativePath());
                            if (!Arrays.equals(Files.readAllBytes(actual), Files.readAllBytes(wanted))) {
                                throw new IllegalStateException("Actual Java file content differs at index " + file.getIndex());
                            }
                        }
                        System.out.println("HANDOFF_FILE\t" + file.getIndex() + "\t" + file.getPriority()
                            + "\t" + file.isSkipped() + "\t" + file.getDownloaded() + "\t" + file.getLength()
                            + "\t" + ByteFormatter.encodeString(actual.toString().getBytes(StandardCharsets.UTF_8)));
                    }
                    checked = true;
                    break; // No startDownload/addPeer: this is an actual read/recheck handoff.
                }
                Thread.sleep(25);
            }
            if (!checked) { throw new IllegalStateException("Original Java checkpoint recheck deadline expired"); }
        } finally {
            if (started) { core.stop(); }
        }
        guard();
        System.out.println("HANDOFF_ORIGINAL_STOPPED");
    }
    public static void main(String[] args) {
        try { run(args); System.exit(0); }
        catch (Throwable failure) { failure.printStackTrace(); System.exit(2); }
    }
}
