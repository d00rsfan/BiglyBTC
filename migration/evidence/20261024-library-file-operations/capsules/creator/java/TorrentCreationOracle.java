import com.biglybt.core.torrent.TOTorrent;
import com.biglybt.core.torrent.TOTorrentAnnounceURLSet;
import com.biglybt.core.torrent.TOTorrentFactory;
import com.biglybt.core.torrent.TOTorrentFile;
import java.io.File;
import java.net.URL;
import java.nio.charset.StandardCharsets;

/** Unchanged retained original torrent decoder only. No original core startup. */
public final class TorrentCreationOracle {
    private static String hex(byte[] bytes) {
        if (bytes == null) { return ""; }
        char[] digits = "0123456789abcdef".toCharArray();
        StringBuilder out = new StringBuilder(bytes.length * 2);
        for (byte value : bytes) { out.append(digits[(value & 255) >>> 4]); out.append(digits[value & 15]); }
        return out.toString();
    }
    public static void main(String[] args) {
        try {
            if (!(System.getSecurityManager() instanceof ReferenceFence) || args.length != 1) {
                throw new IllegalStateException("Owned reference guard and one metadata file required");
            }
            // The original UDP URL handler is needed to parse tracker strings;
            // constructing a URL does not call its openConnection method.
            System.setProperty("java.protocol.handler.pkgs", "com.biglybt.core.util.protocol");
            Class.forName("com.biglybt.core.util.Debug");
            File input = new File(args[0]);
            File root = new File(System.getProperty("biglybtc.test.root")).getCanonicalFile();
            if (!input.getCanonicalPath().startsWith(root.getPath() + File.separator)) {
                throw new IllegalArgumentException("Metadata outside generated fixture");
            }
            TOTorrent torrent = TOTorrentFactory.deserialiseFromBEncodedFile(input);
            System.out.println("HASH\t" + hex(torrent.getHash()));
            System.out.println("TYPE\t" + torrent.getTorrentType());
            System.out.println("NAME\t" + hex(torrent.getName()));
            System.out.println("SIZE\t" + torrent.getSize());
            System.out.println("PIECE_LENGTH\t" + torrent.getPieceLength());
            System.out.println("PRIVATE\t" + torrent.getPrivate());
            System.out.println("COMMENT\t" + hex(torrent.getComment()));
            System.out.println("SIMPLE\t" + torrent.isSimpleTorrent());
            System.out.println("CREATION_DATE\t" + torrent.getCreationDate());
            byte[][] pieces = torrent.getPieces();
            System.out.println("PIECES\t" + pieces.length);
            for (int index = 0; index < pieces.length; ++index) { System.out.println("PIECE\t" + index + "\t" + hex(pieces[index])); }
            TOTorrentFile[] files = torrent.getFiles();
            System.out.println("FILES\t" + files.length);
            for (int index = 0; index < files.length; ++index) {
                StringBuilder path = new StringBuilder();
                for (byte[] component : files[index].getPathComponents()) {
                    if (path.length() > 0) { path.append('/'); } path.append(hex(component));
                }
                System.out.println("FILE\t" + index + "\t" + files[index].getLength() + "\t" + path);
            }
            URL announce = torrent.getAnnounceURL();
            System.out.println("ANNOUNCE\t" + hex(announce.toExternalForm().getBytes(StandardCharsets.UTF_8)));
            TOTorrentAnnounceURLSet[] tiers = torrent.getAnnounceURLGroup().getAnnounceURLSets();
            for (int tier = 0; tier < tiers.length; ++tier) {
                for (URL url : tiers[tier].getAnnounceURLs()) {
                    System.out.println("TRACKER\t" + tier + "\t" + hex(url.toExternalForm().getBytes(StandardCharsets.UTF_8)));
                }
            }
            if (!(System.getSecurityManager() instanceof ReferenceFence)) { throw new IllegalStateException("Guard changed"); }
            System.out.println("ORIGINAL_TORRENT_CREATION_PARSE_PASS");
            System.exit(0);
        } catch (Throwable failure) { failure.printStackTrace(); System.exit(2); }
    }
}
