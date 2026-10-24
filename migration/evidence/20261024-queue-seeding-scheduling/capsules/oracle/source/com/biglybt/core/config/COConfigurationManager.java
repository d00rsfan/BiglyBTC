package com.biglybt.core.config;

import queueoracle.Providers;

/** Only listener registration used by subject class initialization/constructor. */
public final class COConfigurationManager {
    public static void addListener(COConfigurationListener listener) {
        Providers.call("config.addListener");
    }
    public static void addAndFireParameterListener(String key, ParameterListener listener) {
        Providers.call("config.addAndFire:" + key);
        if (!key.equals("droo.features.enable")) { throw Providers.fail("Unexpected config registration: " + key); }
        listener.parameterChanged(key);
    }
    public static boolean getBooleanParameter(String key, boolean fallback) {
        Providers.call("config.getBoolean:" + key);
        if (!key.equals("droo.features.enable") || fallback) { throw Providers.fail("Unexpected config read"); }
        return false;
    }
}
