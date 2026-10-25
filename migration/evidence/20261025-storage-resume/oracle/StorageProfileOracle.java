import com.biglybt.core.Core;
import com.biglybt.core.disk.DiskManagerFileInfo;
import com.biglybt.core.disk.DiskManagerPiece;
import com.biglybt.core.diskmanager.file.FMFile;
import com.biglybt.core.diskmanager.file.FMFileManagerFactory;
import com.biglybt.core.diskmanager.file.FMFileOwner;
import com.biglybt.core.download.DownloadManager;
import com.biglybt.core.download.DownloadManagerInitialisationAdapter;
import com.biglybt.core.torrent.TOTorrent;
import com.biglybt.core.torrent.TOTorrentFactory;
import com.biglybt.core.torrent.TOTorrentFile;
import com.biglybt.core.util.BEncoder;
import com.biglybt.core.util.ByteFormatter;
import com.biglybt.core.util.DirectByteBuffer;
import com.biglybt.core.util.StringInterner;
import java.io.File;
import java.nio.ByteBuffer;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/** Valid original-created profile + actual original disk recheck, not resume synthesis. */
public final class StorageProfileOracle {
    private static final int PIECE = 16384;
    private static final int[] LENGTHS = {3072, 103304, 24696};
    private static final String[] NAMES = {"prefix.bin", "payload.bin", "suffix.bin"};

    private static byte[] pattern(int length, int offset) {
        byte[] result = new byte[length];
        for (int i = 0; i < result.length; ++i) { int p = i + offset; result[i] = (byte)((p * 37 + p / 127 + 19) & 255); }
        return result;
    }

    private static TOTorrent generate(Path root) throws Exception {
        if (Files.exists(root.resolve("input.torrent")) || Files.exists(root.resolve("profile/downloads.config"))) {
            throw new IllegalArgumentException("Fresh generated profile required");
        }
        Path expected = root.resolve("expected/layout"); Files.createDirectories(expected);
        byte[] logical = pattern(PIECE * 8, 0), hashes = new byte[20 * 8];
        MessageDigest digest = MessageDigest.getInstance("SHA-1");
        for (int i = 0; i < 8; ++i) {
            digest.update(logical, i * PIECE, PIECE);
            System.arraycopy(digest.digest(), 0, hashes, i * 20, 20);
        }
        List<Map<String, Object>> files = new ArrayList<>(); int offset = 0;
        for (int i = 0; i < LENGTHS.length; ++i) {
            Map<String, Object> file = new HashMap<>(); file.put("length", (long)LENGTHS[i]);
            file.put("path", Arrays.asList(NAMES[i])); files.add(file);
            Files.write(expected.resolve(NAMES[i]), Arrays.copyOfRange(logical, offset, offset + LENGTHS[i]));
            offset += LENGTHS[i];
        }
        Map<String, Object> info = new HashMap<>(), torrent = new HashMap<>(), opaque = new HashMap<>();
        info.put("name", "layout"); info.put("piece length", (long)PIECE); info.put("pieces", hashes); info.put("files", files);
        torrent.put("info", info); torrent.put("announce", "http://127.0.0.1:1/announce");
        TOTorrent result = TOTorrentFactory.deserialiseFromBEncodedByteArray(new TOTorrentFactory.TorrentDataHolder(BEncoder.encode(torrent)));
        opaque.put("list", Arrays.asList(7L, "storage–π")); result.setAdditionalMapProperty("native.storage.opaque", opaque);
        result.serialiseToBEncodedFile(root.resolve("input.torrent").toFile());
        return result;
    }

    private static FMFile open(final DownloadManager manager, final DiskManagerFileInfo source, int kind) throws Exception {
        FMFileOwner owner = new FMFileOwner() {
            public String getName() { return "owned storage profile " + source.getIndex(); }
            public TOTorrentFile getTorrentFile() { return manager.getTorrent().getFiles()[source.getIndex()]; }
            public File getControlFileDir() { return manager.getDownloadState().getStateDir(); }
            public StringInterner.FileKey getFileLink(StringInterner.FileKey path) { return path; }
            public long getFileAllocationStrategy() { return 0; }
        };
        FMFile result = FMFileManagerFactory.getSingleton().createFile(owner,
            new StringInterner.FileKey(source.getFile(true)), kind, false);
        result.setAccessMode(FMFile.FM_WRITE); return result;
    }

    private static void fill(DownloadManager manager, int kind, Path root) throws Exception {
        DiskManagerFileInfo[] files = manager.getDiskManagerFileInfoSet().getFiles();
        if (files.length != 3 || !files[1].setStorageType(kind)) {
            throw new IllegalStateException("Original refused generated storage mode");
        }
        for (int i = 0; i < files.length; ++i) {
            files[i].setPriority(0); // Original API normal=0; serialized selected value=-1.
            if (i != 1) { files[i].setSkipped(false); }
            Path path = files[i].getFile(true).toPath().toAbsolutePath().normalize();
            if (!path.startsWith(root.resolve("payload")) || Files.isSymbolicLink(path)) {
                throw new IllegalStateException("Original payload mapping escaped generated root");
            }
            Files.createDirectories(path.getParent());
            byte[] bytes = Files.readAllBytes(root.resolve("expected/layout").resolve(NAMES[i]));
            if (i != 1) { Files.write(path, bytes); continue; }
            FMFile target = open(manager, files[i], kind);
            try {
                // Same real file-fragment writes in each mode. Compact itself
                // decides which regions are retained; no map/resume is forged.
                int first = PIECE - LENGTHS[0];
                int[] starts = {0, first + 5 * PIECE, first + PIECE};
                int[] ends = {first, LENGTHS[1], first + 2 * PIECE};
                for (int n = 0; n < starts.length; ++n) {
                    target.write(new DirectByteBuffer(ByteBuffer.wrap(Arrays.copyOfRange(bytes, starts[n], ends[n]))), starts[n]);
                }
                target.setLength(bytes.length); target.flush();
            } finally { target.close(); }
        }
        boolean compact = kind == FMFile.FT_COMPACT || kind == FMFile.FT_PIECE_REORDER_COMPACT;
        if (files[0].isSkipped() || files[2].isSkipped() || files[1].isSkipped() != compact) {
            throw new IllegalStateException("Original format/selection semantics differ");
        }
    }

    private static void observe(DownloadManager manager, Path root, boolean complete, int expectedDone) throws Exception {
        DiskManagerPiece[] pieces = manager.getDiskManager().getPieces();
        int done = 0; StringBuilder bits = new StringBuilder();
        for (DiskManagerPiece piece : pieces) { if (piece.isDone()) { ++done; } bits.append(piece.isDone() ? '1' : '0'); }
        if (pieces.length != 8 || done != expectedDone || (complete && done != 8)) {
            throw new IllegalStateException("Actual original piece recheck differs " + done + "/" + pieces.length);
        }
        System.out.println("PROFILE_READY\t" + done + "\t" + pieces.length + "\t" + bits);
        System.out.println("PROFILE_HASH\t" + ByteFormatter.encodeString(manager.getTorrent().getHash()));
        System.out.println("PROFILE_INTENT\t" + manager.isForceStart() + "\t" + manager.isPaused() + "\t" + manager.getPosition()
            + "\t" + manager.getStats().getDownloadRateLimitBytesPerSecond() + "\t" + manager.getStats().getUploadRateLimitBytesPerSecond());
        System.out.println("PROFILE_COUNTERS\t" + manager.getStats().getTotalDataBytesReceived() + "\t" + manager.getStats().getTotalDataBytesSent());
        DiskManagerFileInfo[] files = manager.getDiskManagerFileInfoSet().getFiles();
        for (int i = 0; i < files.length; ++i) {
            DiskManagerFileInfo file = files[i]; Path path = file.getFile(true).toPath().toAbsolutePath().normalize();
            if (Files.isSymbolicLink(path) || !path.toRealPath().startsWith(root.resolve("payload").toRealPath())) {
                throw new IllegalStateException("Actual original path escaped generated payload");
            }
            System.out.println("PROFILE_FILE\t" + i + "\t" + file.getPriority() + "\t" + file.isSkipped()
                + "\t" + file.getStorageType() + "\t" + file.getDownloaded() + "\t" + file.getLength()
                + "\t" + ByteFormatter.encodeString(path.toString().getBytes("UTF-8")));
            if (complete && (file.isSkipped() || file.getPriority() != 0)) {
                throw new IllegalStateException("Selected original policy not restored");
            }
            if (complete) {
                byte[] wanted = Files.readAllBytes(root.resolve("expected/layout").resolve(NAMES[i]));
                byte[] actual = new byte[wanted.length]; DirectByteBuffer buffer = file.read(0, wanted.length);
                try {
                    if (buffer.getFlag(DirectByteBuffer.FL_CONTAINS_TRANSIENT_DATA)) {
                        throw new IllegalStateException("Actual original complete read contains missing data");
                    }
                    buffer.position(DirectByteBuffer.SS_FILE, 0);
                    buffer.get(DirectByteBuffer.SS_FILE, actual);
                } finally { buffer.returnToPool(); }
                if (!Arrays.equals(wanted, actual)) { throw new IllegalStateException("Actual original logical bytes differ at " + i); }
            }
        }
    }

    private static void run(String[] args) throws Exception {
        if (!(System.getSecurityManager() instanceof ReferenceFence) || args.length != 4) {
            throw new IllegalArgumentException("ReferenceFence: LISTEN prepare|observe KIND EXPECTED_DONE");
        }
        Class.forName("com.biglybt.core.util.Debug");
        int listen = Integer.parseInt(args[0]), kind = Integer.parseInt(args[2]), expectedDone = Integer.parseInt(args[3]);
        boolean prepare = args[1].equals("prepare");
        if ((!prepare && !args[1].equals("observe")) || kind < 2 || kind > 4 || expectedDone < 1 || expectedDone > 8) {
            throw new IllegalArgumentException("Invalid bounded profile case");
        }
        Path root = Paths.get(System.getProperty("biglybtc.test.root")).toRealPath(); Files.createDirectories(root.resolve("payload"));
        if (prepare) { generate(root); }
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
                if (manager == null) { throw new IllegalStateException("Original rejected authentic fixture"); }
                fill(manager, kind, root);
                manager.getStats().setDownloadRateLimitBytesPerSecond(32768);
                manager.getStats().setUploadRateLimitBytesPerSecond(65536);
            } else {
                List<DownloadManager> saved = core.getGlobalManager().getDownloadManagers();
                if (saved.size() != 1) { throw new IllegalStateException("Exactly one original saved download required"); }
                manager = saved.get(0);
            }
            manager.initialize(); long deadline = System.nanoTime() + 90000000000L;
            while (manager.getState() != DownloadManager.STATE_READY) {
                if (manager.getState() == DownloadManager.STATE_ERROR || System.nanoTime() >= deadline) {
                    throw new IllegalStateException("Original READY check failed: " + manager.getErrorDetails());
                }
                Thread.sleep(25);
            }
            observe(manager, root, expectedDone == 8, expectedDone);
            manager.stopIt(DownloadManager.STATE_STOPPED, false, false);
            // This helper never starts a torrent or adds a peer. Core.stop()
            // persists the actual checked state before the native phase opens it.
        } finally { if (started) { core.stop(); } }
        System.out.println("PROFILE_ORIGINAL_STOPPED");
    }
    public static void main(String[] args) {
        try { run(args); System.exit(0); }
        catch (Throwable failure) { failure.printStackTrace(); System.exit(2); }
    }
}
