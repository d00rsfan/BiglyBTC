import com.biglybt.core.diskmanager.file.FMFile;
import com.biglybt.core.diskmanager.file.FMFileManagerFactory;
import com.biglybt.core.diskmanager.file.FMFileOwner;
import com.biglybt.core.torrent.TOTorrentFactory;
import com.biglybt.core.torrent.TOTorrentFile;
import com.biglybt.core.util.BEncoder;
import com.biglybt.core.util.DirectByteBuffer;
import com.biglybt.core.util.StringInterner;
import java.io.File;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Arrays;
import java.util.HashMap;
import java.util.Map;

/** Calls unchanged original FMFile; the tape contains operations, not a codec. */
public final class StorageScriptOracle {
    private static Path owned;
    private static Path root;
    private static TOTorrentFile metadata;
    private static FMFile file;
    private static int kind;

    private static Path leaf(String relative) throws Exception {
        Path path = root.resolve(relative).normalize();
        if (!path.startsWith(root) || path.equals(root) || relative.indexOf('\t') >= 0) {
            throw new IllegalArgumentException("Fixture path escaped its case");
        }
        Path current = root;
        for (Path part : root.relativize(path)) {
            current = current.resolve(part);
            if (Files.isSymbolicLink(current)) { throw new IllegalArgumentException("Fixture symlink"); }
        }
        return path;
    }

    private static FMFile open() throws Exception {
        FMFileOwner owner = new FMFileOwner() {
            public String getName() { return "owned scripted storage " + root; }
            public TOTorrentFile getTorrentFile() { return metadata; }
            public File getControlFileDir() { return root.resolve("control").toFile(); }
            public StringInterner.FileKey getFileLink(StringInterner.FileKey path) { return path; }
            public long getFileAllocationStrategy() { return 0; }
        };
        FMFile result = FMFileManagerFactory.getSingleton().createFile(owner,
            new StringInterner.FileKey(root.resolve("payload.bin.az!").toFile()), kind, false);
        result.setAccessMode(FMFile.FM_WRITE);
        return result;
    }

    private static long number(String value) {
        long result = Long.parseLong(value);
        if (result < 0 || result > 4 * 1024 * 1024) { throw new IllegalArgumentException("Unbounded fixture integer"); }
        return result;
    }

    private static void configure(String[] row) throws Exception {
        if (row.length != 5 || metadata != null) { throw new IllegalArgumentException("Single OPEN required"); }
        kind = (int) number(row[1]);
        long offset = number(row[2]), length = number(row[3]), piece = number(row[4]);
        if (kind < 1 || kind > 4 || piece != 16384 || length == 0) {
            throw new IllegalArgumentException("Bounded original fixture geometry required");
        }
        Map<String, Object> first = new HashMap<>(), second = new HashMap<>(), info = new HashMap<>(), torrent = new HashMap<>();
        first.put("length", offset); first.put("path", Arrays.asList("prefix.bin"));
        second.put("length", length); second.put("path", Arrays.asList("payload.bin"));
        info.put("name", "generated-layout"); info.put("piece length", piece);
        info.put("pieces", new byte[20 * (int)((offset + length + piece - 1) / piece)]);
        info.put("files", Arrays.asList(first, second)); torrent.put("info", info);
        torrent.put("announce", "http://127.0.0.1:1/announce");
        metadata = TOTorrentFactory.deserialiseFromBEncodedByteArray(BEncoder.encode(torrent)).getFiles()[1];
        if (metadata.getOffsetInTorrent() != offset || metadata.getLength() != length) {
            throw new IllegalStateException("Original torrent geometry disagrees");
        }
        file = open();
    }

    private static void run(String[] args) throws Exception {
        if (!(System.getSecurityManager() instanceof ReferenceFence) || args.length != 2) {
            throw new IllegalStateException("Owned ReferenceFence and ROOT TAPE required");
        }
        Class.forName("com.biglybt.core.util.Debug");
        owned = Paths.get(System.getProperty("biglybtc.test.root")).toRealPath();
        root = Paths.get(args[0]).toRealPath();
        Path tape = Paths.get(args[1]).toRealPath();
        if (!root.startsWith(owned) || !tape.startsWith(root) || Files.isSymbolicLink(Paths.get(args[0]))) {
            throw new IllegalArgumentException("Only generated owned fixture paths");
        }
        int step = 0;
        try {
            for (String line : Files.readAllLines(tape, StandardCharsets.UTF_8)) {
                if (line.isEmpty() || line.startsWith("#")) { continue; }
                if (++step > 128) { throw new IllegalArgumentException("Too many fixture operations"); }
                String[] row = line.split("\t", -1);
                String op = row[0];
                if (op.equals("OPEN")) { configure(row); }
                else if (op.equals("REOPEN") && row.length == 1 && file == null && metadata != null) { file = open(); }
                else if (op.equals("CLOSE") && row.length == 1 && file != null) { file.close(); file = null; }
                else {
                    if (file == null) { throw new IllegalArgumentException("Operation needs an open original file"); }
                    if (op.equals("WRITE") && row.length == 3) {
                        byte[] bytes = Files.readAllBytes(leaf(row[2]));
                        if (bytes.length > 65536) { throw new IllegalArgumentException("Unbounded fixture write"); }
                        file.write(new DirectByteBuffer(ByteBuffer.wrap(bytes)), number(row[1]));
                    } else if (op.equals("READ") && row.length == 4 && row[3].matches("[a-z0-9-]{1,40}")) {
                        byte[] bytes = new byte[(int) number(row[2])]; Arrays.fill(bytes, (byte) 0xA5);
                        DirectByteBuffer buffer = new DirectByteBuffer(ByteBuffer.wrap(bytes));
                        file.read(buffer, number(row[1]));
                        Files.write(leaf("read-" + row[3] + ".bin"), bytes);
                        System.out.println("READ\t" + step + "\t" + row[3] + "\t" + buffer.position(DirectByteBuffer.SS_FILE)
                            + "\t" + buffer.getFlag(DirectByteBuffer.FL_CONTAINS_TRANSIENT_DATA));
                    } else if (op.equals("LENGTH") && row.length == 2) { file.setLength(number(row[1])); }
                    else if (op.equals("COMPLETE") && row.length == 3) {
                        byte[] bytes = Files.readAllBytes(leaf(row[2]));
                        if (bytes.length > 16384) { throw new IllegalArgumentException("Piece fragment too large"); }
                        file.setPieceComplete((int)number(row[1]), new DirectByteBuffer(ByteBuffer.wrap(bytes)));
                    } else if (op.equals("FLUSH") && row.length == 1) { file.flush(); }
                    else { throw new IllegalArgumentException("Invalid fixture operation " + op); }
                }
                if (file != null) { System.out.println("STATE\t" + step + "\t" + op + "\t" + file.getLength() + "\t" + file.getStorageType()); }
                else { System.out.println("STATE\t" + step + "\t" + op + "\tclosed"); }
            }
        } finally { if (file != null) { file.close(); file = null; } }
        System.out.println("SCRIPT_ORIGINAL_STOPPED\t" + step);
    }
    public static void main(String[] args) {
        try { run(args); System.exit(0); }
        catch (Throwable failure) { failure.printStackTrace(); System.exit(2); }
    }
}
