package com.biglybt.core.util;

import queueoracle.Providers;

/** RankCalculatorReal has one unused activation pool static field. No task runs. */
public final class ThreadPool<T extends AERunnable> {
    public ThreadPool(String name, int size, boolean queue) {
        Providers.call("activation.pool.constructor");
        if (!name.equals("StartStopRules:activate") || size != 32 || !queue) {
            throw Providers.fail("Unexpected activation pool");
        }
    }
    public boolean isFull() { throw Providers.fail("Activation not admitted"); }
}
