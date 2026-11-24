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

import static com.facebook.presto.plugin.clp.ClpRuntimeMetrics.GET_SPLITS_NANOS;
import static com.facebook.presto.plugin.clp.ClpRuntimeMetrics.LIST_SPLITS_NANOS;
import static com.facebook.presto.plugin.clp.ClpRuntimeMetrics.SPLIT_COUNT;
import static java.util.Objects.requireNonNull;

import java.util.List;
import javax.inject.Inject;

import com.facebook.presto.spi.ConnectorSession;
import com.facebook.presto.spi.ConnectorSplitSource;
import com.facebook.presto.spi.ConnectorTableLayoutHandle;
import com.facebook.presto.spi.FixedSplitSource;
import com.facebook.presto.spi.connector.ConnectorSplitManager;
import com.facebook.presto.spi.connector.ConnectorTransactionHandle;

import com.facebook.presto.plugin.clp.split.ClpSplitProvider;

public class ClpSplitManager implements ConnectorSplitManager {
    private final ClpSplitProvider clpSplitProvider;

    @Inject
    public ClpSplitManager(ClpSplitProvider clpSplitProvider) {
        this.clpSplitProvider = requireNonNull(clpSplitProvider, "clpSplitProvider is null");
    }

    @Override
    public ConnectorSplitSource getSplits(
            ConnectorTransactionHandle transactionHandle,
            ConnectorSession session,
            ConnectorTableLayoutHandle layout,
            SplitSchedulingContext splitSchedulingContext
    ) {
        long start = System.nanoTime();
        ClpTableLayoutHandle layoutHandle = (ClpTableLayoutHandle)layout;
        // The split providers have no session; they leave their own timers pending on this thread.
        ClpRuntimeMetrics.clearPending();
        List<ClpSplit> splits;
        try {
            splits = clpSplitProvider.listSplits(layoutHandle);
        } finally {
            ClpRuntimeMetrics.addNanos(session, LIST_SPLITS_NANOS, System.nanoTime() - start);
            ClpRuntimeMetrics.flushPending(session);
        }
        ClpRuntimeMetrics.addCount(session, SPLIT_COUNT, splits.size());
        ConnectorSplitSource splitSource = new FixedSplitSource(splits);
        ClpRuntimeMetrics.addNanos(session, GET_SPLITS_NANOS, System.nanoTime() - start);
        return splitSource;
    }
}
