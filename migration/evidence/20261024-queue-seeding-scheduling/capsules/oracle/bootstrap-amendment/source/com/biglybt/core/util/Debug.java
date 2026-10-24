package com.biglybt.core.util;

import queueoracle.Providers;

/** Unexpected diagnostics are latched even if original code catches Throwable. */
public final class Debug {
    public static void out(String value) { throw Providers.fail("Unexpected Debug.out: " + value); }
    public static void out(Throwable value) { throw Providers.fail("Unexpected Debug.out: " + value); }
    public static void out(String value, Throwable error) { throw Providers.fail("Unexpected Debug.out: " + value); }
    public static void outNoStack(String value) { throw Providers.fail("Unexpected Debug.outNoStack: " + value); }
    public static void printStackTrace(Throwable value) { throw Providers.fail("Unexpected Debug.printStackTrace: " + value); }
}
