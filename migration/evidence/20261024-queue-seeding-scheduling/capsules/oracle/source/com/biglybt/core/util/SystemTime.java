package com.biglybt.core.util;

import queueoracle.Providers;

/** Explicit supplied clocks. No host clock, sleeping, updating thread or timer. */
public final class SystemTime {
    private static long wall, mono;
    public static void supply(long wallMillis, long monoMillis) { wall = wallMillis; mono = monoMillis; }
    public static long getCurrentTime() { Providers.call("clock.wall"); return wall; }
    public static long getMonotonousTime() { Providers.call("clock.mono"); return mono; }
}
