/*
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
package com.facebook.presto.plugin.clp;

import com.facebook.presto.common.RuntimeStats;
import com.facebook.presto.common.RuntimeUnit;
import com.facebook.presto.spi.ConnectorSession;

import java.util.LinkedHashMap;
import java.util.Map;

/**
 * thesis-metrics: timers and counters of the coordinator side of the connector. Every value is
 * added to the runtime statistics of the query the {@link ConnectorSession} belongs to, so it
 * appears in the query's {@code queryStats.runtimeStats}. Nothing here changes what the connector
 * does: a value that cannot be recorded is dropped.
 * <p>
 * Code that has no session at hand (the cache loaders of {@link ClpMetadata}, the split providers)
 * records into a list that belongs to the calling thread; the caller that has the session moves
 * that list into the query's statistics with {@link #flushPending}. The cache loaders and the
 * split providers run on the thread of the call that needs them.
 */
public final class ClpThesisMetrics
{
    private static final ThreadLocal<Map<String, long[]>> PENDING = ThreadLocal.withInitial(LinkedHashMap::new);

    private ClpThesisMetrics() {}

    public static void addNanos(ConnectorSession session, String name, long nanos)
    {
        add(session, name, RuntimeUnit.NANO, nanos);
    }

    public static void addCount(ConnectorSession session, String name, long value)
    {
        add(session, name, RuntimeUnit.NONE, value);
    }

    /**
     * Records a duration for the caller, on this thread, that has the session.
     */
    public static void pendingNanos(String name, long nanos)
    {
        // [0] is the unit (1 for nanoseconds), [1] the value.
        PENDING.get().put(name, new long[] {1, nanos});
    }

    /**
     * Records a count for the caller, on this thread, that has the session.
     */
    public static void pendingCount(String name, long value)
    {
        PENDING.get().put(name, new long[] {0, value});
    }

    /**
     * Drops what an earlier call on this thread recorded and nobody collected.
     */
    public static void clearPending()
    {
        PENDING.get().clear();
    }

    /**
     * @return whether anything was pending
     */
    public static boolean flushPending(ConnectorSession session)
    {
        Map<String, long[]> pending = PENDING.get();
        if (pending.isEmpty()) {
            return false;
        }
        for (Map.Entry<String, long[]> entry : pending.entrySet()) {
            long[] unitAndValue = entry.getValue();
            add(session, entry.getKey(), 1 == unitAndValue[0] ? RuntimeUnit.NANO : RuntimeUnit.NONE, unitAndValue[1]);
        }
        pending.clear();
        return true;
    }

    private static void add(ConnectorSession session, String name, RuntimeUnit unit, long value)
    {
        try {
            if (null == session) {
                return;
            }
            RuntimeStats runtimeStats = session.getRuntimeStats();
            if (null != runtimeStats) {
                runtimeStats.addMetricValue(name, unit, value);
            }
        }
        catch (RuntimeException e) {
            // A session without runtime statistics: the value is not recorded.
        }
    }
}
