package com.biglybt.plugin.startstoprules.defaultplugin;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Set;

import com.biglybt.core.download.DownloadManagerState;
import com.biglybt.core.util.SystemTime;
import com.biglybt.pif.PluginConfig;
import com.biglybt.pif.download.Download;
import com.biglybt.pif.download.DownloadScrapeResult;
import com.biglybt.pif.download.DownloadStats;
import com.biglybt.pifimpl.local.PluginCoreUtils;
import queueoracle.Providers;

/** Host fixtures around unchanged retained RankCalculatorReal bytecode. No rank arithmetic. */
public final class QueueRankOracle {
    private static final class Fixture implements InvocationHandler {
        final Map<String, Long> values = new HashMap<>();
        DownloadStats stats;
        DownloadScrapeResult scrape;
        DownloadManagerState state;
        Download.SeedingRank rank;
        long value(String name) {
            if (!values.containsKey(name)) { throw Providers.fail("Missing explicit fixture field " + name); }
            return values.get(name);
        }
        @Override public Object invoke(Object proxy, Method method, Object[] arguments) {
            String name = method.getName();
            Providers.call(method.getDeclaringClass().getSimpleName() + "." + name);
            if (name.equals("hashCode")) { return System.identityHashCode(proxy); }
            if (name.equals("equals")) { return proxy == arguments[0]; }
            if (name.equals("toString")) { return "ExplicitQueueFixture"; }
            switch (name) {
                case "getStats": return stats;
                case "getDownloadState": return state;
                case "getDiskManager": case "getPeerManager": case "getLastAnnounceResult": return null;
                case "getName": return "owned-queue-fixture";
                case "getState": return Math.toIntExact(value("state"));
                case "getPosition": return Math.toIntExact(value("position"));
                case "isComplete": return value("complete") != 0;
                case "isPersistent": return value("persistent") != 0;
                case "isForceStart": return value("forceStart") != 0;
                case "isChecking": return value("checking") != 0;
                case "isMoving": return value("moving") != 0;
                case "getAggregatedScrapeResult": return scrape;
                case "getSeedingRank": return rank;
                case "setSeedingRank": rank = (Download.SeedingRank)arguments[0]; return null;
                case "getSeedCount": return Math.toIntExact(value("seeds"));
                case "getNonSeedCount": return Math.toIntExact(value("peers"));
                case "getResponseType": return Math.toIntExact(value("scrapeResponse"));
                case "getScrapeStartTime": return value("scrapeMillis");
                case "getShareRatio": return Math.toIntExact(value("shareRatio"));
                case "getSecondsOnlySeeding": return value("secondsSeeding");
                case "getSecondsDownloading": return value("secondsDownloading");
                case "getSecondsSinceLastUpload": return value("secondsSinceLastUpload");
                case "getTimeStarted": return value("startedMillis");
                case "getTimeStartedSeeding": return value("seedingStartedMillis");
                case "getDownloadAverage": return value("downloadRate");
                case "getUploadAverage": return value("uploadRate");
                case "getTransientFlags": return value("transientFlags");
                case "getIntParameter": {
                    String key = (String)arguments[0];
                    if (key.equals("sr.min")) { return Math.toIntExact(value("minimumShareRatio")); }
                    if (key.equals("sr.max")) { return Math.toIntExact(value("maximumShareRatio")); }
                    throw Providers.fail("Unexpected int state key " + key);
                }
                case "getLongParameter": {
                    String key = (String)arguments[0];
                    if (key.equals("stats.download.last.active.time")) { return value("lastActiveMillis"); }
                    if (key.equals("stats.download.completed.time")) { return value("completedMillis"); }
                    throw Providers.fail("Unexpected long state key " + key);
                }
                case "addListener": case "removeListener": return null;
                default: throw Providers.fail("Unexpected proxy method " + method);
            }
        }
    }
    private static <T> T proxy(Class<T> type, InvocationHandler handler) {
        return type.cast(Proxy.newProxyInstance(QueueRankOracle.class.getClassLoader(), new Class<?>[]{type}, handler));
    }
    private static void run(String id, Map<String, Object> config, Fixture fixture, long wall, long mono) throws Exception {
        Providers.reset();
        SystemTime.supply(wall, mono);
        PluginConfig settings = proxy(PluginConfig.class, (self, method, arguments) -> {
            Providers.call("PluginConfig." + method.getName());
            if (!method.getName().startsWith("getUnsafe") || arguments == null || arguments.length < 1) {
                throw Providers.fail("Unexpected plugin configuration method " + method);
            }
            String key = (String)arguments[0];
            if (!config.containsKey(key)) { throw Providers.fail("Missing explicit config input " + key); }
            Object result = config.get(key);
            if (method.getReturnType() == Integer.TYPE && result instanceof Number) { return ((Number)result).intValue(); }
            if (method.getReturnType() == Float.TYPE && result instanceof Number) { return ((Number)result).floatValue(); }
            if (method.getReturnType() == Boolean.TYPE && result instanceof Boolean) { return result; }
            throw Providers.fail("Wrong explicit config type: " + key);
        });
        fixture.stats = proxy(DownloadStats.class, fixture);
        fixture.scrape = proxy(DownloadScrapeResult.class, fixture);
        fixture.state = proxy(DownloadManagerState.class, fixture);
        Download download = proxy(Download.class, fixture);
        com.biglybt.core.download.DownloadManager core = proxy(com.biglybt.core.download.DownloadManager.class, fixture);
        PluginCoreUtils.link(download, core);
        StartStopRulesDefaultPlugin plugin = new StartStopRulesDefaultPlugin();
        plugin.plugin_config = settings;
        RankCalculatorReal subject = new RankCalculatorReal(plugin, download);
        // Public actual reload avoids the first constructor's captured static config listener binding later cases.
        RankCalculatorReal.reloadConfigParams(settings);
        if (fixture.value("lastActiveMillis") != 0 || fixture.value("completedMillis") != 0) {
            throw Providers.fail("Uncontrolled java.lang.System clock branch not admitted");
        }
        subject.recalcSeedingRank();
        boolean firstPriority = subject.isFirstPriority();
        Providers.verify();
        System.out.println("OBS\t" + id + "\tseedingRank\t" + subject.getSeedingRank());
        System.out.println("OBS\t" + id + "\tfirstPriority\t" + (firstPriority ? 1 : 0));
        System.out.println("TRACE\t" + id + "\t" + Providers.counts());
        subject.destroy();
        PluginCoreUtils.clear();
        Providers.verify();
    }
    public static void main(String[] arguments) throws Exception {
        Providers.verify();
        BufferedReader input = new BufferedReader(new InputStreamReader(System.in, "UTF-8"));
        Map<String, Object> config = null;
        Fixture fixture = null;
        Set<String> ids = new HashSet<>();
        String id = null;
        long wall = 0, mono = 0;
        int count = 0;
        for (String line; (line = input.readLine()) != null;) {
            if (line.isEmpty() || line.startsWith("#")) { continue; }
            String[] fields = line.split("\t", -1);
            switch (fields[0]) {
                case "CASE":
                    if (fields.length != 2 || id != null || !ids.add(fields[1])) { throw Providers.fail("Invalid CASE"); }
                    id = fields[1]; config = new LinkedHashMap<>(); fixture = new Fixture(); break;
                case "CFG": {
                    if (fields.length != 4 || id == null || config.containsKey(fields[1])) { throw Providers.fail("Invalid CFG"); }
                    Object value;
                    switch (fields[2]) {
                        case "I": value = Integer.valueOf(fields[3]); break;
                        case "B":
                            if (!fields[3].equals("0") && !fields[3].equals("1")) { throw Providers.fail("Invalid boolean"); }
                            value = fields[3].equals("1"); break;
                        case "F": value = Float.valueOf(fields[3]); break;
                        default: throw Providers.fail("Invalid config type");
                    }
                    config.put(fields[1], value); break;
                }
                case "FACT":
                    if (fields.length != 3 || id == null || fixture.values.put(fields[1], Long.valueOf(fields[2])) != null) {
                        throw Providers.fail("Invalid FACT");
                    }
                    break;
                case "CLOCK":
                    if (fields.length != 3 || id == null) { throw Providers.fail("Invalid CLOCK"); }
                    wall = Long.parseLong(fields[1]); mono = Long.parseLong(fields[2]); break;
                case "END":
                    if (fields.length != 1 || id == null || ++count > 1000) { throw Providers.fail("Invalid END"); }
                    run(id, config, fixture, wall, mono); id = null; break;
                default: throw Providers.fail("Unknown fixture command");
            }
        }
        if (id != null || count == 0) { throw Providers.fail("Incomplete/empty fixture"); }
        Providers.verify();
        System.out.println("PASS_QUEUE_RANK_ORIGINAL\t" + count + "\t" + count * 2);
    }
}
