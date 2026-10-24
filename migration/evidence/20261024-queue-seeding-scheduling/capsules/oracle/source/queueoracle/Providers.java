package queueoracle;

import java.util.LinkedHashMap;
import java.util.Map;

/** New inert providers only; no original decision method is reproduced here. */
public final class Providers {
    private static final Thread OWNER = Thread.currentThread();
    private static final Map<String, Integer> CALLS = new LinkedHashMap<>();
    private static boolean violated;
    private static int count;
    public static void call(String key) {
        if (Thread.currentThread() != OWNER || ++count > 20000) {
            throw fail("Unexpected thread or provider call bound");
        }
        CALLS.put(key, CALLS.containsKey(key) ? CALLS.get(key) + 1 : 1);
    }
    public static AssertionError fail(String reason) {
        violated = true;
        return new AssertionError(reason);
    }
    public static void reset() { CALLS.clear(); count = 0; }
    public static void verify() {
        if (violated) { throw new AssertionError("Latched provider violation"); }
        if (!System.getSecurityManager().getClass().getName().equals("QueueOracleFence")) {
            throw fail("Queue fence missing/replaced");
        }
    }
    public static String counts() { return CALLS.toString(); }
}
