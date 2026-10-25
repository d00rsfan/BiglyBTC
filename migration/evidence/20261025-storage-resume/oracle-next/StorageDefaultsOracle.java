import com.biglybt.core.Core;
import com.biglybt.core.config.COConfigurationManager;
import com.biglybt.core.disk.DiskManagerFileInfo;
import com.biglybt.core.disk.DiskManagerPiece;
import com.biglybt.core.download.DownloadManager;
import com.biglybt.core.download.DownloadManagerInitialisationAdapter;
import com.biglybt.core.torrent.TOTorrent;
import com.biglybt.core.torrent.TOTorrentFactory;
import com.biglybt.core.util.BEncoder;
import com.biglybt.core.util.ByteFormatter;
import com.biglybt.core.util.DirectByteBuffer;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/** New fixture only. All root/default/link decisions are made by retained core. */
public final class StorageDefaultsOracle {
    private static final int PIECE = 16384;
    private static final int TOTAL = 8 * PIECE;
    private static final int[] MULTI_LENGTHS = {3072, 103304, 24696};
    private static final String[] MULTI_NAMES = {"prefix.bin", "payload.bin", "suffix.bin"};

    private static String hex(String value) throws Exception {
        return ByteFormatter.encodeString(value.getBytes("UTF-8"));
    }

    private static void generate(Path root, boolean multi) throws Exception {
        if (Files.exists(root.resolve("input.torrent")) || Files.exists(root.resolve("profile/downloads.config"))) {
            throw new IllegalArgumentException("A fresh owned case is required");
        }
        byte[] data = new byte[TOTAL];
        for (int i = 0; i < data.length; ++i) { data[i] = (byte)((i * 37 + i / 127 + 19) & 255); }
        byte[] hashes = new byte[20 * 8];
        MessageDigest sha1 = MessageDigest.getInstance("SHA-1");
        for (int i = 0; i < 8; ++i) {
            sha1.update(data, i * PIECE, PIECE);
            System.arraycopy(sha1.digest(), 0, hashes, i * 20, 20);
        }
        Map<String,Object> info = new HashMap<String,Object>();
        info.put("name", multi ? "layout" : "single.bin");
        info.put("piece length", (long)PIECE); info.put("pieces", hashes);
        if (multi) {
            Files.createDirectories(root.resolve("expected/layout"));
            List<Map<String,Object>> files = new ArrayList<Map<String,Object>>();
            int offset = 0;
            for (int i = 0; i < MULTI_LENGTHS.length; ++i) {
                Map<String,Object> file = new HashMap<String,Object>();
                file.put("length", (long)MULTI_LENGTHS[i]);
                file.put("path", Arrays.asList(MULTI_NAMES[i])); files.add(file);
                Files.write(root.resolve("expected/layout").resolve(MULTI_NAMES[i]),
                    Arrays.copyOfRange(data, offset, offset + MULTI_LENGTHS[i]));
                offset += MULTI_LENGTHS[i];
            }
            info.put("files", files);
        } else {
            Files.createDirectories(root.resolve("expected"));
            info.put("length", (long)TOTAL); Files.write(root.resolve("expected/single.bin"), data);
        }
        Map<String,Object> torrent = new HashMap<String,Object>();
        torrent.put("info", info); torrent.put("announce", "http://127.0.0.1:1/announce");
        TOTorrent value = TOTorrentFactory.deserialiseFromBEncodedByteArray(
            new TOTorrentFactory.TorrentDataHolder(BEncoder.encode(torrent)));
        Map<String,Object> opaque = new HashMap<String,Object>();
        opaque.put("list", Arrays.asList(-7L, "save-defaults–π"));
        value.setAdditionalMapProperty("native.defaults.opaque", opaque);
        value.serialiseToBEncodedFile(root.resolve("input.torrent").toFile());
    }

    private static Path checked(Path root, Path path) throws Exception {
        Path value = path.toAbsolutePath().normalize();
        if (!value.startsWith(root) || Files.isSymbolicLink(value)) {
            throw new IllegalStateException("Original mapping escaped owned case");
        }
        return value;
    }

    private static void observe(DownloadManager manager, Path root, boolean complete) throws Exception {
        System.out.println("MAPPING_ROOT\t" + hex(checked(root, manager.getSaveLocation().toPath()).toString()));
        System.out.println("MAPPING_HASH\t" + ByteFormatter.encodeString(manager.getTorrent().getHash()));
        DiskManagerFileInfo[] files = manager.getDiskManagerFileInfoSet().getFiles();
        StringBuilder stores = new StringBuilder();
        for (int i = 0; i < files.length; ++i) {
            DiskManagerFileInfo file = files[i];
            Path actual = checked(root, file.getFile(true).toPath());
            Path base = checked(root, file.getFile(false).toPath());
            if (stores.length() > 0) { stores.append(','); }
            stores.append(file.getStorageType());
            System.out.println("MAPPING_FILE\t" + i + "\t" + file.getStorageType()
                + "\t" + file.isSkipped() + "\t" + file.getPriority() + "\t" + file.getLength()
                + "\t" + hex(base.toString()) + "\t" + hex(actual.toString()));
            if (complete) {
                Path expected = manager.getTorrent().isSimpleTorrent() ? root.resolve("expected/single.bin")
                    : root.resolve("expected/layout").resolve(MULTI_NAMES[i]);
                byte[] wanted = Files.readAllBytes(expected), observed = new byte[wanted.length];
                DirectByteBuffer buffer = file.read(0, wanted.length);
                try {
                    if (buffer.getFlag(DirectByteBuffer.FL_CONTAINS_TRANSIENT_DATA)) {
                        throw new IllegalStateException("Complete original reload contains missing data");
                    }
                    buffer.position(DirectByteBuffer.SS_FILE, 0); buffer.get(DirectByteBuffer.SS_FILE, observed);
                } finally { buffer.returnToPool(); }
                if (!Arrays.equals(wanted, observed)) { throw new IllegalStateException("Original logical reload differs at " + i); }
            }
        }
        System.out.println("MAPPING_TYPES\t" + stores);
        System.out.println("MAPPING_THRESHOLD\t" + (manager.getDownloadState().hasAttribute("reordermb")
            ? Integer.toString(manager.getDownloadState().getIntAttribute("reordermb")) : "absent"));
        System.out.println("MAPPING_COUNTERS\t" + manager.getStats().getTotalDataBytesReceived()
            + "\t" + manager.getStats().getTotalDataBytesSent());
        if (complete) {
            DiskManagerPiece[] pieces = manager.getDiskManager().getPieces(); int done = 0;
            for (DiskManagerPiece piece : pieces) { if (piece.isDone()) { ++done; } }
            if (pieces.length != 8 || done != 8) { throw new IllegalStateException("Original full recheck differs " + done); }
            System.out.println("MAPPING_VERIFIED\t" + done + "\t" + TOTAL);
        }
    }

    private static void run(String[] args) throws Exception {
        if (!(System.getSecurityManager() instanceof ReferenceFence) || args.length != 3) {
            throw new IllegalArgumentException("ReferenceFence LISTEN prepare|probe|observe single|multi required");
        }
        Class.forName("com.biglybt.core.util.Debug");
        int listen = Integer.parseInt(args[0]); String phase = args[1];
        boolean multi = args[2].equals("multi"), prepare = phase.equals("prepare");
        if ((!multi && !args[2].equals("single")) || (!prepare && !phase.equals("probe") && !phase.equals("observe"))) {
            throw new IllegalArgumentException("Invalid bounded defaults case");
        }
        Path root = Paths.get(System.getProperty("biglybtc.test.root")).toRealPath();
        if (prepare) { generate(root, multi); }
        Core core = EngineOracle.configuredCore(listen); boolean started = false;
        // Suffix handling already has separate qualification. Keep this family
        // focused on root/default/negative-link decisions and exact paths.
        COConfigurationManager.setParameter("Rename Incomplete Files", false);
        try {
            core.start(); started = true; DownloadManager manager;
            if (prepare) {
                Files.createDirectories(root.resolve("payload/initial"));
                manager = core.getGlobalManager().addDownloadManager(root.resolve("input.torrent").toString(), null,
                    root.resolve("payload/initial").toString(), DownloadManager.STATE_STOPPED, true, false,
                    new DownloadManagerInitialisationAdapter() {
                        public int getActions() { return ACT_NONE; }
                        public void initialised(DownloadManager download, boolean seeding) {
                            download.getDownloadState().setNetworks(new String[]{"Public"});
                            download.getDownloadState().setPeerSources(new String[]{"Plugin", "Incoming"});
                        }
                    });
                if (manager == null) { throw new IllegalStateException("Original rejected generated metadata"); }
                for (DiskManagerFileInfo file : manager.getDiskManagerFileInfoSet().getFiles()) {
                    file.setPriority(0); file.setSkipped(false);
                }
            } else {
                List<DownloadManager> values = core.getGlobalManager().getDownloadManagers();
                if (values.size() != 1) { throw new IllegalStateException("Exactly one generated download is required"); }
                manager = values.get(0);
            }
            if (phase.equals("observe")) {
                manager.initialize(); long deadline = System.nanoTime() + 90000000000L;
                while (manager.getState() != DownloadManager.STATE_READY) {
                    if (manager.getState() == DownloadManager.STATE_ERROR || System.nanoTime() >= deadline) {
                        throw new IllegalStateException("Original READY failed: " + manager.getErrorDetails());
                    }
                    Thread.sleep(25);
                }
            }
            observe(manager, root, phase.equals("observe"));
            if (phase.equals("observe")) { manager.stopIt(DownloadManager.STATE_STOPPED, false, false); }
            // prepare/probe never initialize/start a download or add a peer.
        } finally { if (started) { core.stop(); } }
        System.out.println("MAPPING_STOPPED");
    }

    public static void main(String[] args) {
        try { run(args); System.exit(0); }
        catch (Throwable failure) { failure.printStackTrace(); System.exit(2); }
    }
}
