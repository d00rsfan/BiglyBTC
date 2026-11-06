// SPDX-License-Identifier: GPL-2.0-or-later
// Source proposal only. Protected saved-value construction is intentional;
// no Core, subscription/metasearch manager, search or network method is used.
import com.biglybt.core.metasearch.FilterableResult;
import com.biglybt.core.metasearch.impl.web.WebEngine;
import com.biglybt.core.metasearch.impl.web.WebResult;
import com.biglybt.core.metasearch.impl.web.rss.RSSEngine;
import com.biglybt.core.subs.Subscription;
import com.biglybt.core.subs.impl.SubscriptionResultImpl;
import com.biglybt.core.subs.util.SubscriptionResultFilterable;
import com.biglybt.pif.utils.search.SearchResult;
import com.biglybt.util.JSONUtils;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

@SuppressWarnings({"rawtypes", "unchecked"})
public final class SavedResultValueOracle {
	// Actual Original saved-map constructor with no history instance. Its static
	// default-time initializer may touch ONLY the future private config tree.
	// None of the observed methods below needs history; accidental authority or
	// mutation calls fail visibly rather than substituting a fake manager.
	private static final class ReadOnlySavedResult extends SubscriptionResultImpl {
		ReadOnlySavedResult(Map values) { super(null, values); }
		@Override public Subscription getSubscription() { throw new AssertionError("No subscription authority"); }
		@Override public void setRead(boolean read) { throw new AssertionError("No result mutation"); }
		@Override public void delete() { throw new AssertionError("No result deletion"); }
	}
	private static Map<String,Object> map(Object... values) {
		Map<String,Object> result = new LinkedHashMap<>();
		for (int i = 0; i < values.length; i += 2) { result.put((String) values[i], values[i + 1]); }
		return result;
	}
	private static Map savedMap(String json) {
		return map("key", new byte[]{1,2,3,4,5,6,7,8,9,10}, "read", 0L,
				"tz", 1L, "result_json", json.getBytes(StandardCharsets.UTF_8));
	}
	private static void noManagerLink(Map json) {
		// Actual Result.adjustLink unconditionally obtains MetaSearchManager for
		// h/H/f/F strings of length>=5. Such fixture fields are forbidden here,
		// including property-map links that are not selected by getDownloadLink.
		for (String key : new String[]{"dbl", "dl", "cdp", "pl"}) {
			Object value = json.get(key);
			if (value instanceof String) {
				String text = (String) value;
				if (text.length() >= 5 && "hHfF".indexOf(text.charAt(0)) >= 0) {
					throw new AssertionError("Forbidden manager-backed adjustLink fixture");
				}
			}
		}
	}
	private static void add(List<Map<String,Object>> rows, String label, String json,
			boolean malformedObservationOnly) {
		Map decoded = JSONUtils.decodeJSON(json);
		if (decoded == null) { throw new AssertionError("Fixture JSON must be valid"); }
		noManagerLink(decoded);
		ReadOnlySavedResult actual = new ReadOnlySavedResult(savedMap(json));
		Map<String,Object> row = map("label", label, "saved_json", json,
				"malformed_original_observation_only", malformedObservationOnly);
		try { row.put("actual_getDownloadLink_opaque_or_magnet_only", actual.getDownloadLink()); }
		catch (RuntimeException error) { row.put("download_link_error", error.getClass().getSimpleName()); }
		Map<Integer,Object> properties;
		try { properties = actual.toPropertyMap(); }
		catch (RuntimeException error) {
			row.put("property_map_error", error.getClass().getSimpleName()); rows.add(row); return;
		}
		Long rawSeeds = (Long) properties.get(SearchResult.PR_SEED_COUNT);
		Long rawPeers = (Long) properties.get(SearchResult.PR_LEECHER_COUNT);
		row.put("actual_property_values", map("size", properties.get(SearchResult.PR_SIZE),
				"seeds", rawSeeds, "peers", rawPeers, "rank", properties.get(SearchResult.PR_RANK),
				"download_link", properties.get(SearchResult.PR_DOWNLOAD_LINK),
				"torrent_link", properties.get(SearchResult.PR_TORRENT_LINK)));
		// This formula is explicitly source-derived from prepareDownload. No
		// DownloadManager or prepareDownload execution is manufactured here.
		Long packed = rawSeeds != null && rawPeers != null && rawSeeds >= 0 && rawPeers >= 0
				? ((rawSeeds & 0x00ffffffL) << 32) | (rawPeers & 0x00ffffffL) : null;
		row.put("source_derived_cache_formula_not_manager_execution", packed);
		SubscriptionResultFilterable saved = new SubscriptionResultFilterable(null, actual);
		row.put("actual_saved_filterable", map("size", saved.getSize(),
				"seeds", (long) saved.getNbSeeds(), "peers", (long) saved.getNbPeers(),
				"torrent_link", saved.getTorrentLink(), "time", saved.getTime(),
				"seeds_peers_display", saved.getSeedsPeers()));
		FilterableResult direct = actual.getFilterableResult();
		Map<String,Object> directValues = map("seeds", (long) direct.getNbSeeds(),
				"peers", (long) direct.getNbPeers());
		try { directValues.put("size", direct.getSize()); }
		catch (RuntimeException error) { directValues.put("size_error", error.getClass().getSimpleName()); }
		row.put("actual_direct_result_filterable", directValues);
		rows.add(row);
	}
	public static void main(String[] args) {
		if (args.length != 0) { throw new IllegalArgumentException("No caller profile/URL/runtime arguments"); }
		List<Map<String,Object>> rows = new ArrayList<>();
		add(rows, "missing-saved-metrics", "{\"n\":\"A\",\"dl\":\"opaque:download\"}", false);
		add(rows, "negative-raw-counts", "{\"n\":\"A\",\"s\":\"-1\",\"p\":\"-2\"}", false);
		add(rows, "known-zero-counts", "{\"n\":\"A\",\"lb\":\"0\",\"s\":\"0\",\"p\":\"0\"}", false);
		add(rows, "positive-int-wrap", "{\"n\":\"A\",\"lb\":\"1\",\"s\":\"4294967297\",\"p\":\"2147483648\"}", false);
		add(rows, "long-max-counts", "{\"n\":\"A\",\"s\":\"9223372036854775807\",\"p\":\"9223372036854775807\"}", false);
		add(rows, "long-min-size-and-count", "{\"n\":\"A\",\"lb\":\"-9223372036854775808\",\"s\":\"-9223372036854775808\",\"p\":\"0\"}", false);
		add(rows, "opaque-button-precedes-download", "{\"n\":\"A\",\"dbl\":\"opaque:button\",\"dl\":\"opaque:download\"}", false);
		add(rows, "null-button-falls-back", "{\"n\":\"A\",\"dbl\":null,\"dl\":\"opaque:download\"}", false);
		add(rows, "magnet-button-prefers-any-download", "{\"n\":\"A\",\"dbl\":\"MaGnEt:?xt=one\",\"dl\":\"opaque:download\"}", false);
		add(rows, "magnet-button-prefers-explicit-magnet", "{\"n\":\"A\",\"dbl\":\"magnet:?xt=one\",\"dl\":\"magnet:?xt=two\"}", false);
		add(rows, "empty-button-does-not-fall-back", "{\"n\":\"A\",\"dbl\":\"\",\"dl\":\"opaque:download\"}", false);
		add(rows, "play-is-not-download", "{\"n\":\"A\",\"pl\":\"opaque:play\"}", false);
		add(rows, "button-without-download", "{\"n\":\"A\",\"dbl\":\"opaque:button\"}", false);
		add(rows, "ordinary-rank-property", "{\"n\":\"A\",\"r\":\"0.25\",\"s\":\"3\",\"p\":\"4\"}", false);
		for (String field : new String[]{"\"r\":\"bad\"", "\"c\":3", "\"ct\":3", "\"cdp\":3", "\"tgs\":[1]",
				"\"s\":\"bad\"", "\"ts\":\"bad\"", "\"v\":\"bad\""}) {
			String prefix = field.startsWith("\"s\":") ? "{\"n\":\"A\",\"p\":\"4\","
					: "{\"n\":\"A\",\"s\":\"3\",\"p\":\"4\",";
			add(rows, "malformed-property-" + rows.size(), prefix + field + "}", true);
		}
		WebResult fresh = new WebResult(null, "", "", null, "");
		Map<String,Object> freshValues = map("size", fresh.getSize(),
				"seeds", (long) fresh.getNbSeeds(), "peers", (long) fresh.getNbPeers());
		List<Map<String,Object>> referers = new ArrayList<>();
		for (String configured : new String[]{"http://feeds.example.org/feed.xml",
				"https://feeds.example.org:8443/dir/feed.xml?x=a/b", "tor:https://feeds.example.org/dir/feed.xml",
				"https://feeds.example.org/feed.xml"}) {
			// Genuine unregistered public RSSEngine, not a fake Engine subclass.
			// Positive id7 uses deterministic UID allocation and never calls the
			// null MetaSearchImpl's configDirty branch. No search/setter is invoked.
			RSSEngine actual = new RSSEngine(null, 7L, 0L, 1.0f, "Value only", configured,
					false, WebEngine.AM_TRANSPARENT, null, new String[0]);
			referers.add(map("configured_url_value", configured,
					"actual_public_WebEngine_getReferer", actual.getReferer()));
		}
		Map deleted = savedMap("{\"n\":\"unused tombstone\"}"); deleted.put("deleted", 0L); deleted.put("dls", 7L);
		ReadOnlySavedResult tombstone = new ReadOnlySavedResult(deleted);
		System.out.println(JSONUtils.encodeToJSON(map(
				"schema", "BIGLYBT_SAVED_RESULT_GETTER_VALUE_OBSERVATIONS_V1",
				"saved_case_count", (long) rows.size(), "saved_cases", rows,
				"fresh_WebResult_actual_getters", freshValues,
				"engine_RootPage_referer_values", referers,
				"actual_deleted_zero_means_present_tombstone", tombstone.isDeleted(),
				"HTTP_adjustLink_or_full_HTTP_getter_executed", false,
				"manager_prepareDownload_executed", false)));
		System.out.println("PASS_ORIGINAL_SAVED_RESULT_VALUE_GETTERS_LIMITED_SCOPE");
	}
}
