package com.biglybt.core.util;

import com.biglybt.core.download.DownloadManager;
import queueoracle.Providers;

/** Explicit no-moving-task fixture; avoids original global Core lookup. */
public final class FileUtil {
    public static boolean hasTask(DownloadManager manager) {
        Providers.call("fileTask.absent");
        if (manager == null) { throw Providers.fail("Missing core proxy"); }
        return false;
    }
}
