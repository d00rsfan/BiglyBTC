import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

import com.biglybt.core.config.COConfigurationManager;
import com.biglybt.core.download.DownloadManagerState;
import com.biglybt.core.download.DownloadManagerStateFactory;
import com.biglybt.core.torrent.TOTorrent;
import com.biglybt.core.torrent.TOTorrentFactory;
import com.biglybt.core.util.BEncoder;
import com.biglybt.core.util.SystemProperties;
import com.biglybt.util.JSONUtils;

/** Future owned-profile reference subject; no Core/GlobalManager/plugin startup. */
public final class OriginalDownloadStateHandback {
  private static final String KEY = "Plugin.<internal>.azsubs.subs_info";
  private static final String RETURN_KEY = "native_original_handback";
  private static final byte[] RETURN_VALUE =
      "public-DownloadManagerState-V1".getBytes(StandardCharsets.UTF_8);
  private static final LinkOption NOFOLLOW = LinkOption.NOFOLLOW_LINKS;

  private static void require(boolean ok, String message) {
    if (!ok) throw new IllegalStateException(message);
  }
  private static byte[] hex(String text, int bytes) {
    require(text.matches("[0-9A-F]{" + (bytes * 2) + "}"), "Noncanonical hex");
    byte[] out = new byte[bytes];
    for (int i = 0; i < bytes; ++i)
      out[i] = (byte)Integer.parseInt(text.substring(i * 2, i * 2 + 2), 16);
    return out;
  }
  private static String hex(byte[] bytes) {
    StringBuilder out = new StringBuilder();
    for (byte value : bytes) out.append(String.format("%02X", value & 255));
    return out.toString();
  }
  private static String sha(byte[] bytes) throws Exception {
    return hex(MessageDigest.getInstance("SHA-256").digest(bytes));
  }
  private static byte[] read(Path path) throws Exception {
    require(Files.isRegularFile(path, NOFOLLOW) && Files.size(path) > 0 &&
        Files.size(path) <= 64L * 1024 * 1024, "Missing/unsafe/big primary input");
    return Files.readAllBytes(path);
  }
  private static byte[] optional(Path path) throws Exception {
    return Files.exists(path, NOFOLLOW) ? read(path) : null;
  }
  private static void same(Path path, byte[] expected) throws Exception {
    require(Arrays.equals(optional(path), expected), "Unexpected file mutation: " + path);
  }
  private static Map attrs(TOTorrent torrent) {
    Map values = torrent.getAdditionalMapProperty("attributes");
    require(values != null, "No actual attributes map");
    return values;
  }
  private static List<String> sids(Map map) {
    require(map != null && map.get("s") instanceof List, "No actual SID list");
    List<?> values = (List<?>)map.get("s");
    require(values.size() <= 4096, "SID list bound");
    List<String> out = new ArrayList<>();
    for (Object value : values) {
      require(value instanceof byte[], "Malformed actual SID");
      out.add(hex((byte[])value));
    }
    return out;
  }

  // --inspect|--handback OWNED_PROFILE UPPERCASE_V1_HASH UPPERCASE_SELECTED_SID
  // EXPECTED_SCSRC EXPECTED_RAW64_CACHE. The subject never generates Native inputs.
  @SuppressWarnings({"rawtypes", "unchecked"})
  public static void main(String[] args) throws Exception {
    require(args.length == 6 && (args[0].equals("--inspect") ||
        args[0].equals("--handback")), "Closed subject arguments");
    boolean handback = args[0].equals("--handback");
    Path root = Path.of(args[1]).toRealPath();
    require(root.isAbsolute() && root.toString().equals(
        System.getenv("BIGLYBTC_OWNED_STATE_HANDBACK_ROOT")), "Unowned subject root");
    require(Files.isDirectory(root.resolve("active"), NOFOLLOW) &&
        Files.isDirectory(root.resolve("torrents"), NOFOLLOW), "No actual Native profile");
    byte[] hash = hex(args[2], 20), selectedSid = hex(args[3], 10);
    long source = Long.parseLong(args[4]), cache = Long.parseLong(args[5]);
    require(source == 0 || source == 1, "Unexpected source kind");
    Path statePath = root.resolve("active/" + args[2] + ".dat");
    Path backupPath = Path.of(statePath + ".bak");
    Path archivePath = root.resolve("torrents/" + args[2] + ".torrent");
    Path inventoryPath = root.resolve("downloads.config");
    Path output = root.resolve("original-download-state-handback.json");
    require(!Files.exists(output, NOFOLLOW), "Output already exists");
    for (String name : new String[]{"BiglyBTC.subscription-acquire",
        "BiglyBTC.subscription-acquire.saving", "BiglyBTC.subscription-acquire.bak",
        "BiglyBTC.torrent-import", "BiglyBTC.torrent-import.saving", "BiglyBTC.torrent-import.bak"})
      require(!Files.exists(root.resolve(name), NOFOLLOW), "Pending acquisition/intake");
    for (String suffix : new String[]{".saving", "._az"})
      require(!Files.exists(Path.of(statePath + suffix), NOFOLLOW), "Pending state writer");
    byte[] before = read(statePath), backup = optional(backupPath);
    byte[] archive = read(archivePath), inventory = read(inventoryPath);

    // Must precede all Original file/config/torrent initialization.
    System.setProperty("azureus.config.path", root.toString());
    SystemProperties.setUserPath(root.toString());
    COConfigurationManager.setParameter("Save Torrent Backup", false);
    COConfigurationManager.setParameter("Tracker DNS Records Enable", false);
    TOTorrent original = TOTorrentFactory.deserialiseFromBEncodedFile(archivePath.toFile());
    TOTorrent primary = TOTorrentFactory.deserialiseFromBEncodedFile(statePath.toFile());
    require(original.getTorrentType() == TOTorrent.TT_V1 &&
        primary.getTorrentType() == TOTorrent.TT_V1 &&
        Arrays.equals(hash, original.getHash()) &&
        Arrays.equals(hash, primary.getHash()), "Wrong actual torrent/state identity");
    Map beforeAttrs = BEncoder.cloneMap(attrs(primary));
    require(!(beforeAttrs.containsKey("category")), "Category initialization outside scope");
    Object flags = beforeAttrs.get("flags");
    require(flags == null || (flags instanceof Long &&
        (((Long)flags) & DownloadManagerState.FLAG_DISABLE_IP_FILTER) == 0),
        "IP-filter initialization outside scope");
    require(Long.valueOf(source).equals(beforeAttrs.get("scsrc")) &&
        Long.valueOf(cache).equals(beforeAttrs.get("scrapecache")), "Actual cache differs");
    Map beforeSubs = (Map)beforeAttrs.get(KEY);
    List<String> beforeSids = sids(beforeSubs);
    require(beforeSids.contains(hex(selectedSid)), "Actual source SID missing");
    require(!beforeSubs.containsKey(RETURN_KEY), "Handback already performed");

    DownloadManagerState state = DownloadManagerStateFactory.getDownloadState(original);
    require(state.getDownloadManager() == null && !state.getAndClearRecoveredStatus(),
        "Unexpected manager or recreated state");
    require(BEncoder.mapsAreIdentical(beforeSubs, state.getMapAttribute(KEY)) &&
        state.getLongAttribute(DownloadManagerState.AT_SCRAPE_CACHE_SOURCE) == source &&
        state.getLongAttribute(DownloadManagerState.AT_SCRAPE_CACHE) == cache,
        "Public state getter mismatch");
    same(statePath, before); same(backupPath, backup);
    Map constructedAttrs = BEncoder.cloneMap(attrs(state.getTorrent()));
    long returnedCache = cache;
    if (handback) {
      Map updated = BEncoder.cloneMap(beforeSubs);
      updated.put(RETURN_KEY, RETURN_VALUE.clone());
      state.setMapAttribute(KEY, updated);
      Map expected = BEncoder.cloneMap(constructedAttrs);
      expected.put(KEY, updated); expected.put("scsrc", Long.valueOf(source));
      expected.put("scrapecache", Long.valueOf(returnedCache));
      state.save(false); // Actual void API swallows I/O failures: re-read PRIMARY below.
      TOTorrent saved = TOTorrentFactory.deserialiseFromBEncodedFile(statePath.toFile());
      require(Arrays.equals(hash, saved.getHash()) &&
          BEncoder.mapsAreIdentical(expected, attrs(saved)), "Public save did not persist");
      require(sids((Map)attrs(saved).get(KEY)).equals(beforeSids), "SID list changed");
      same(backupPath, before);
    }
    same(archivePath, archive); same(inventoryPath, inventory);
    for (String suffix : new String[]{".saving", "._az"})
      require(!Files.exists(Path.of(statePath + suffix), NOFOLLOW), "Unfinished state save");
    Map<String,Object> result = new LinkedHashMap<>();
    result.put("schema", "BIGLYBT_PUBLIC_DOWNLOAD_STATE_PROPERTY_HANDBACK_V1");
    result.put("status", handback ? "PASS_PUBLIC_STATE_PROPERTY_HANDBACK" :
        "PASS_PUBLIC_STATE_PROPERTY_READ");
    result.put("Original_commit", "cb8c6cb9bb97916cbc7e80338a017200ffc3e71f");
    result.put("hash", args[2]); result.put("selected_sid", args[3]);
    result.put("sids_before_and_after", beforeSids);
    result.put("scsrc", Long.toString(source));
    result.put("cache_before", Long.toString(cache));
    result.put("cache_after", Long.toString(returnedCache));
    result.put("state_before_sha256", sha(before));
    result.put("state_after_sha256", sha(read(statePath)));
    result.put("constructor_attributes_changed",
        !BEncoder.mapsAreIdentical(beforeAttrs, constructedAttrs));
    result.put("input_attributes_sha256", sha(BEncoder.encode(beforeAttrs)));
    result.put("constructor_attributes_sha256", sha(BEncoder.encode(constructedAttrs)));
    result.put("manager_attached", false); result.put("handback", handback);
    Files.writeString(output, JSONUtils.encodeToJSON(result) + "\n",
        StandardCharsets.UTF_8, StandardOpenOption.CREATE_NEW);
    System.out.println(handback ? "PASS_ORIGINAL_PUBLIC_DOWNLOAD_STATE_HANDBACK" :
        "PASS_ORIGINAL_PUBLIC_DOWNLOAD_STATE_READ");
  }
}
