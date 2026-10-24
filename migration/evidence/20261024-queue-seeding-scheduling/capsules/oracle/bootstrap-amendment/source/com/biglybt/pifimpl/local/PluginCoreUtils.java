package com.biglybt.pifimpl.local;

import java.util.IdentityHashMap;
import com.biglybt.pif.download.Download;
import com.biglybt.core.download.DownloadManager;
import queueoracle.Providers;

/** Explicit host binding for proxy Downloads; no concrete DownloadImpl/Core. */
public final class PluginCoreUtils {
    private static final IdentityHashMap<Download, DownloadManager> LINKS = new IdentityHashMap<>();
    public static void link(Download download, DownloadManager manager) {
        if (LINKS.size() >= 4 || LINKS.put(download, manager) != null) { throw Providers.fail("Proxy link bound/duplicate"); }
    }
    public static void clear() { LINKS.clear(); }
    public static DownloadManager unwrap(Download download) {
        Providers.call("unwrap.explicitBinding");
        DownloadManager result = LINKS.get(download);
        if (result == null) { throw Providers.fail("Unbound Download proxy"); }
        return result;
    }
}
