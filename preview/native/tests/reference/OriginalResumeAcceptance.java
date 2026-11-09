// SPDX-License-Identifier: GPL-2.0-or-later
// Test bridge into the source-built original predicate, never a copied model.
import com.biglybt.core.disk.impl.resume.RDResumeHandler;
import com.biglybt.core.util.BDecoder;
import com.biglybt.core.config.impl.ConfigurationManager;
import java.util.HashMap;
import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.lang.reflect.Method;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.Map;
import java.util.zip.GZIPInputStream;
public final class OriginalResumeAcceptance {
    public static void main(String[] args) throws Exception {
        if (args.length != 1 || System.getSecurityManager() == null) {
            throw new IllegalArgumentException("Isolated generated checkpoint required");
        }
        byte[] bytes = Files.readAllBytes(Paths.get(args[0]));
        if (bytes.length > 2 && (bytes[0] & 255) == 31 && (bytes[1] & 255) == 139) {
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            try (GZIPInputStream in = new GZIPInputStream(new ByteArrayInputStream(bytes))) {
                byte[] block = new byte[8192]; int count;
                while ((count = in.read(block)) != -1) {
                    if (out.size() + count > 1024 * 1024) { throw new IllegalArgumentException("Fixture bound"); }
                    out.write(block, 0, count);
                }
            }
            bytes = out.toByteArray();
        }
        Map root = BDecoder.decode(bytes);
        Map data = (Map)((Map)root.get("resume")).get("data");
        byte[] pieces = (byte[])data.get("resume data");
        // Seed the original in-memory configuration without starting a client.
        ConfigurationManager.getInstance(new HashMap<String, Object>());
        Method predicate = RDResumeHandler.class.getDeclaredMethod("isTorrentResumeDataComplete", int.class, Map.class);
        predicate.setAccessible(true);
        if (!Boolean.TRUE.equals(predicate.invoke(null, pieces.length, data))) {
            throw new AssertionError("Original Java rejects the actual clean Native checkpoint");
        }
        System.out.println("PASS original Java recognises actual early-exit Native checkpoint as complete");
        System.out.println("ORIGIN " + RDResumeHandler.class.getProtectionDomain().getCodeSource().getLocation());
        System.exit(0);
    }
}
