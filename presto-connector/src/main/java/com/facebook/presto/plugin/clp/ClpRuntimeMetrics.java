/*
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
package com.facebook.presto.plugin.clp;

import java.util.ArrayList;
import java.util.List;
import java.util.function.Supplier;

import com.facebook.presto.common.RuntimeStats;
import com.facebook.presto.common.RuntimeUnit;
import com.facebook.presto.spi.ConnectorSession;

/**
 * Timers and counters of the connector's coordinator side. Each value is added to the runtime
 * statistics of the query that the {@link ConnectorSession} belongs to, so it appears in the
 * query's {@code queryStats.runtimeStats}. The names are listed in docs/runtime-metrics.md.
 * Measurement only: nothing in the connector reads these values, and one that cannot be recorded
 * is dropped.
 * <p>
 * Code without a session at hand (the loaders of {@link ClpMetadata}'s caches, the split
 * providers) records into a list that belongs to the calling thread, and the caller that has the
 * session moves the list into the query's statistics with {@link #flushPending}. Those loaders and
 * providers run on the thread of the call that needs them.
 */
public final class ClpRuntimeMetrics {
    public static final String METADATA_GET_TABLE_HANDLE_NANOS = "clpMetadataGetTableHandleNanos";
    public static final String METADATA_GET_TABLE_METADATA_NANOS
            = "clpMetadataGetTableMetadataNanos";
    public static final String METADATA_GET_COLUMN_HANDLES_NANOS
            = "clpMetadataGetColumnHandlesNanos";
    public static final String METADATA_LIST_TABLES_NANOS = "clpMetadataListTablesNanos";
    public static final String METADATA_LIST_TABLE_COLUMNS_NANOS
            = "clpMetadataListTableColumnsNanos";

    public static final String TABLE_HANDLE_CACHE_LOOKUP_NANOS = "clpTableHandleCacheLookupNanos";
    public static final String TABLE_HANDLE_CACHE_HITS = "clpTableHandleCacheHits";
    public static final String TABLE_HANDLE_CACHE_MISSES = "clpTableHandleCacheMisses";
    public static final String TABLE_HANDLE_CACHE_LOAD_NANOS = "clpTableHandleCacheLoadNanos";
    public static final String COLUMN_HANDLE_CACHE_LOOKUP_NANOS = "clpColumnHandleCacheLookupNanos";
    public static final String COLUMN_HANDLE_CACHE_HITS = "clpColumnHandleCacheHits";
    public static final String COLUMN_HANDLE_CACHE_MISSES = "clpColumnHandleCacheMisses";
    public static final String COLUMN_HANDLE_CACHE_LOAD_NANOS = "clpColumnHandleCacheLoadNanos";

    public static final String UDF_REWRITER_NANOS = "clpUdfRewriterNanos";
    public static final String COMPUTE_PUSH_DOWN_NANOS = "clpComputePushDownNanos";

    public static final String GET_SPLITS_NANOS = "clpGetSplitsNanos";
    public static final String LIST_SPLITS_NANOS = "clpListSplitsNanos";
    public static final String SPLIT_COUNT = "clpSplitCount";
    public static final String LIST_SPLITS_CONNECT_NANOS = "clpListSplitsConnectNanos";
    public static final String LIST_SPLITS_QUERY_NANOS = "clpListSplitsQueryNanos";
    public static final String LIST_SPLITS_FETCH_NANOS = "clpListSplitsFetchNanos";

    private static final ThreadLocal<List<PendingValue>> PENDING = ThreadLocal.withInitial(
            ArrayList::new
    );

    private ClpRuntimeMetrics() {}

    /**
     * Runs {@code body} and adds its wall time to the query's statistics, also when it throws.
     *
     * @param session the query's session
     * @param name the metric's name
     * @param body the code to time
     * @param <T> the type of the result
     * @return what {@code body} returns
     */
    public static <T> T time(ConnectorSession session, String name, Supplier<T> body) {
        long start = System.nanoTime();
        try {
            return body.get();
        } finally {
            addNanos(session, name, System.nanoTime() - start);
        }
    }

    public static void addNanos(ConnectorSession session, String name, long nanos) {
        add(session, name, RuntimeUnit.NANO, nanos);
    }

    public static void addCount(ConnectorSession session, String name, long value) {
        add(session, name, RuntimeUnit.NONE, value);
    }

    /**
     * Records a duration on this thread, for the caller that has the session to flush.
     *
     * @param name the metric's name
     * @param nanos the duration
     */
    public static void pendingNanos(String name, long nanos) {
        PENDING.get().add(new PendingValue(name, nanos));
    }

    /**
     * Drops what an earlier call on this thread recorded and nobody flushed.
     */
    public static void clearPending() {
        PENDING.get().clear();
    }

    /**
     * Moves what this thread recorded into the query's statistics.
     *
     * @param session the query's session
     * @return whether anything was pending
     */
    public static boolean flushPending(ConnectorSession session) {
        List<PendingValue> pending = PENDING.get();
        if (pending.isEmpty()) { return false; }
        for (PendingValue value : pending) {
            addNanos(session, value.name, value.nanos);
        }
        pending.clear();
        return true;
    }

    private static void add(ConnectorSession session, String name, RuntimeUnit unit, long value) {
        if (null == session) { return; }
        try {
            RuntimeStats runtimeStats = session.getRuntimeStats();
            if (null != runtimeStats) {
                runtimeStats.addMetricValue(name, unit, value);
            }
        } catch (UnsupportedOperationException e) {
            // A session without runtime statistics: the value is not recorded.
        }
    }

    private static final class PendingValue {
        private final String name;
        private final long nanos;

        private PendingValue(String name, long nanos) {
            this.name = name;
            this.nanos = nanos;
        }
    }
}
