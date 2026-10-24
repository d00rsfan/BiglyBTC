package com.biglybt.core.util;

import java.util.function.Supplier;
import queueoracle.Providers;

/**
 * Signature-only provider for original proxy return types. No interning,
 * filesystem values, EMPTY instances or cleanup threads are supplied.
 * The scalar subject must never construct or use one of these values.
 */
public final class StringInterner {
    private StringInterner() { throw Providers.fail("Unused StringInterner constructed"); }

    public interface StringSupplier extends Supplier<String>, Comparable<StringSupplier> {
        boolean isEmpty();
    }

    public static class FileKey implements StringSupplier {
        protected FileKey() { throw Providers.fail("Unused FileKey constructed"); }
        @Override public String get() { throw Providers.fail("Unused FileKey.get invoked"); }
        @Override public boolean isEmpty() { throw Providers.fail("Unused FileKey.isEmpty invoked"); }
        @Override public int compareTo(StringSupplier other) {
            throw Providers.fail("Unused FileKey.compareTo invoked");
        }
    }

    public static class DirKey extends FileKey {
        private DirKey() { super(); }
    }
}
