// SPDX-License-Identifier: GPL-2.0-or-later
import com.biglybt.core.Core;
import com.biglybt.core.download.DownloadManager;
import com.biglybt.core.util.BEncoder;
import com.biglybt.core.util.ByteFormatter;
import com.biglybt.pif.download.Download;
import com.biglybt.pif.download.DownloadStub;
import com.biglybt.pifimpl.local.PluginCoreUtils;

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.security.MessageDigest;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/** Lifecycle oracle for original BiglyBT cb8c6cb9bb97916cbc7e80338a017200ffc3e71f, using EngineOracle/ReferenceFence. */
public final class BiglyBTCArchiveLifecycleOracle {
    private static final byte[] PAYLOAD = "Archived payload\n".getBytes(StandardCharsets.UTF_8);
    private static final byte[] AUXILIARY = {0, 42, -1};

    private static void require(boolean condition, String message) {
        if (!condition) {
            throw new IllegalStateException(message);
        }
    }

    private static DownloadManager createVerifiedDownload(Core core, Path root) throws Exception {
        Path payload = Files.createDirectory(root.resolve("payload"));
        Files.write(payload.resolve("archive-payload.bin"), PAYLOAD);
        Map<String, Object> info = new HashMap<>();
        info.put("length", (long) PAYLOAD.length);
        info.put("name", "archive-payload.bin");
        info.put("piece length", 16384L);
        info.put("pieces", MessageDigest.getInstance("SHA-1").digest(PAYLOAD));
        Map<String, Object> torrent = new HashMap<>();
        torrent.put("announce", "http://127.0.0.1:1/announce");
        torrent.put("info", info);
        Path metadata = Files.write(root.resolve("input.torrent"), BEncoder.encode(torrent));
        DownloadManager download = core.getGlobalManager().addDownloadManager(metadata.toString(), null,
                payload.toString(), DownloadManager.STATE_STOPPED, true, false, null);
        require(download != null, "Original rejected the generated torrent");
        download.initialize();
        long deadline = System.nanoTime() + 20_000_000_000L;
        while (download.getState() != DownloadManager.STATE_READY) {
            require(download.getState() != DownloadManager.STATE_ERROR && System.nanoTime() < deadline,
                    "Original disk verification failed: " + download.getErrorDetails());
            Thread.sleep(25);
        }
        require(download.getDiskManager().getPieces()[0].isDone(), "Payload piece was not verified");
        download.stopIt(DownloadManager.STATE_STOPPED, false, false);
        require(download.isDownloadComplete(false), "Stopped download no longer reports verified completion");
        return download;
    }

    private static void archive(Core core, Path root, Path profile) throws Exception {
        DownloadManager download = createVerifiedDownload(core, root);
        Path stateDir = download.getDownloadState().getStateDir().toPath();
        Files.createDirectories(stateDir.resolve("nested"));
        Files.write(stateDir.resolve("nested/fixture.bin"), AUXILIARY);
        Download pluginDownload = PluginCoreUtils.wrap(download);
        require(pluginDownload.canStubbify(), "Verified original download cannot be archived");
        DownloadStub stub = pluginDownload.stubbify();
        String hash = ByteFormatter.encodeString(stub.getTorrentHash());
        Path archive = profile.resolve("dlarchive");
        require(Files.isRegularFile(archive.resolve(hash + ".dat")), "Missing original archived state");
        require(Arrays.equals(Files.readAllBytes(archive.resolve(hash + "/nested/fixture.bin")), AUXILIARY),
                "Original archive lost the auxiliary tree");
        require(core.getGlobalManager().getDownloadManagers().isEmpty(), "Archive retained an active row");
        require(Arrays.equals(Files.readAllBytes(Paths.get(stub.getSavePath())), PAYLOAD), "Archive changed payload");
        System.out.println("JAVA_ARCHIVED " + hash);
    }

    private static void restore(Core core, Path profile) throws Exception {
        DownloadStub[] stubs = core.getPluginManager().getDefaultPluginInterface().getDownloadManager().getDownloadStubs();
        require(stubs.length == 1, "Exactly one persisted original stub is required");
        DownloadStub stub = stubs[0];
        String hash = ByteFormatter.encodeString(stub.getTorrentHash());
        Path expected = Paths.get(stub.getSavePath());
        Download restored = stub.destubbify();
        Path restoredPath = Paths.get(restored.getSavePath());
        require(restoredPath.equals(expected) && Arrays.equals(Files.readAllBytes(restoredPath), PAYLOAD),
                "Restore changed the save path or payload");
        require(Arrays.equals(Files.readAllBytes(profile.resolve("active/" + hash + "/nested/fixture.bin")), AUXILIARY),
                "Original restore lost auxiliary files");
        require(!Files.exists(profile.resolve("dlarchive/" + hash + ".dat")), "Restore kept the archived state file");
        require(core.getGlobalManager().getDownloadManagers().size() == 1, "Restore did not add one active row");
        System.out.println("JAVA_RESTORED " + hash);
    }

    private static void observe(Core core, Path root, Path profile) throws Exception {
        List<DownloadManager> downloads = core.getGlobalManager().getDownloadManagers();
        require(downloads.size() == 1, "Original did not reopen exactly one native-restored torrent");
        DownloadManager restored = downloads.get(0);
        require(restored.getState() == DownloadManager.STATE_STOPPED && restored.isDownloadComplete(false),
                "Original restarted or lost the persisted verified state");
        Path payload = root.resolve("payload/archive-payload.bin");
        require(restored.getSaveLocation().toPath().equals(payload) && Arrays.equals(Files.readAllBytes(payload), PAYLOAD),
                "Native restore changed the original save path or payload");
        String hash = ByteFormatter.encodeString(restored.getTorrent().getHash());
        require(Arrays.equals(Files.readAllBytes(profile.resolve("active/" + hash + "/nested/fixture.bin")), AUXILIARY),
                "Native restore lost original auxiliary files");
        require(core.getPluginManager().getDefaultPluginInterface().getDownloadManager().getDownloadStubs().length == 0,
                "Original still sees a duplicate archived torrent");
        System.out.println("JAVA_REOPENED_NATIVE_RESTORE " + hash);
    }

    public static void main(String[] args) throws Exception {
        require(args.length == 2 && System.getSecurityManager() instanceof ReferenceFence,
                "Usage under ReferenceFence: archive|restore|observe PORT");
        Path root = Paths.get(System.getProperty("biglybtc.test.root")).toRealPath();
        Path profile = root.resolve("profile");
        require(profile.equals(Paths.get(System.getProperty("azureus.config.path"))), "Use the isolated profile");
        Core core = EngineOracle.configuredCore(Integer.parseInt(args[1]));
        core.start();
        try {
            if (args[0].equals("archive")) {
                archive(core, root, profile);
            } else if (args[0].equals("restore")) {
                restore(core, profile);
            } else if (args[0].equals("observe")) {
                observe(core, root, profile);
            } else {
                throw new IllegalArgumentException("Unknown lifecycle step");
            }
        } finally {
            core.stop();
        }
    }
}
