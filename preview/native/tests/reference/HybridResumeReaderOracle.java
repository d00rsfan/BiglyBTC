// SPDX-License-Identifier: GPL-2.0-or-later
// Actual original APIs and predicate; not a reimplementation of resume rules.
import com.biglybt.core.config.impl.ConfigurationManager;
import com.biglybt.core.disk.impl.resume.RDResumeHandler;
import com.biglybt.core.torrent.TOTorrent;
import com.biglybt.core.torrent.TOTorrentFactory;
import com.biglybt.core.util.BDecoder;
import java.lang.reflect.Method;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Arrays;
import java.util.HashMap;
import java.util.Map;
import java.util.List;
import java.util.stream.Collectors;
public final class HybridResumeReaderOracle {
    private static void need(boolean value, String why) {
        if (!value) { throw new AssertionError(why); }
    }
    public static void main(String[] args) throws Exception {
        need(System.getSecurityManager() instanceof HybridResumeFence, "JDK17 reference fence required");
        Path root = Paths.get(System.getProperty("biglybtc.test.root")).toRealPath();
        Path expectedClasses = Paths.get(System.getProperty("biglybtc.test.classes")).toRealPath();
        need(Paths.get(RDResumeHandler.class.getProtectionDomain().getCodeSource().getLocation().toURI()).toRealPath().equals(expectedClasses), "Unexpected original Java class origin");
        ConfigurationManager.getInstance(new HashMap<String, Object>());
        Method predicate = RDResumeHandler.class.getDeclaredMethod("isTorrentResumeDataComplete", int.class, Map.class);
        predicate.setAccessible(true);
        List<Path> cases;
        try (java.util.stream.Stream<Path> files = Files.list(root.resolve("checkpoints"))) {
            cases = files.filter(p -> p.getFileName().toString().endsWith(".dat")).sorted().collect(Collectors.toList());
        }
        need(cases.size() == 9, "Exactly nine generated actual Native checkpoints required");
        int complete = 0, partial = 0;
        for (Path file : cases) {
            byte[] raw = Files.readAllBytes(file);
            need(raw.length <= 1024 * 1024, "Generated checkpoint byte bound");
            TOTorrent torrent = TOTorrentFactory.deserialiseFromBEncodedFile(file.toFile());
            need(torrent.getTorrentType() == TOTorrent.TT_V1_V2, "Original Java must retain hybrid metadata");
            byte[] v1 = torrent.getFullHash(TOTorrent.TT_V1), v2 = torrent.getFullHash(TOTorrent.TT_V2);
            need(v1 != null && v1.length == 20 && v2 != null && v2.length == 32, "Both full identities required");
            need(Arrays.equals(torrent.getHash(), Arrays.copyOf(v2, 20)), "Original Java selected a different swarm");
            Map map = BDecoder.decode(raw);
            Map data = (Map)((Map)map.get("resume")).get("data");
            byte[] pieces = (byte[])data.get("resume data");
            need(pieces.length == torrent.getNumberOfPieces(), "Actual Java/native piece geometry differs");
            boolean expected = file.getFileName().toString().endsWith(".complete.dat");
            need(Boolean.TRUE.equals(predicate.invoke(null, pieces.length, data)) == expected, "Original Java rejects Native checkpoint meaning");
            if (expected) { ++complete; } else { ++partial; }
        }
        need(complete == 6 && partial == 3, "Actual complete and partial checkpoints required");
        System.out.println("PASS_ORIGINAL_HYBRID_RESUME cases=9 complete=6 partial=3 selected=v2 geometry=matched");
        System.exit(0);
    }
}
