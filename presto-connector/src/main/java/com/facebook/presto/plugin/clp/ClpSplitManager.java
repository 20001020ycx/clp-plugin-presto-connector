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

import com.facebook.presto.plugin.clp.split.ClpSplitProvider;
import com.facebook.presto.spi.ConnectorSession;
import com.facebook.presto.spi.ConnectorSplitSource;
import com.facebook.presto.spi.ConnectorTableLayoutHandle;
import com.facebook.presto.spi.FixedSplitSource;
import com.facebook.presto.spi.connector.ConnectorSplitManager;
import com.facebook.presto.spi.connector.ConnectorTransactionHandle;

import javax.inject.Inject;

import java.util.List;

import static java.util.Objects.requireNonNull;

public class ClpSplitManager
        implements ConnectorSplitManager
{
    private final ClpSplitProvider clpSplitProvider;

    @Inject
    public ClpSplitManager(ClpSplitProvider clpSplitProvider)
    {
        this.clpSplitProvider = requireNonNull(clpSplitProvider, "clpSplitProvider is null");
    }

    @Override
    public ConnectorSplitSource getSplits(
            ConnectorTransactionHandle transactionHandle,
            ConnectorSession session,
            ConnectorTableLayoutHandle layout,
            SplitSchedulingContext splitSchedulingContext)
    {
        // thesis-metrics: the split provider has no session; it leaves its own timers pending on
        // this thread.
        long thesisStart = System.nanoTime();
        ClpThesisMetrics.clearPending();
        ClpTableLayoutHandle layoutHandle = (ClpTableLayoutHandle) layout;
        List<ClpSplit> splits = clpSplitProvider.listSplits(layoutHandle);
        ClpThesisMetrics.addNanos(session, "clpListSplitsNanos", System.nanoTime() - thesisStart);
        ClpThesisMetrics.flushPending(session);
        ClpThesisMetrics.addCount(session, "clpSplitCount", splits.size());
        ConnectorSplitSource splitSource = new FixedSplitSource(splits);
        ClpThesisMetrics.addNanos(session, "clpGetSplitsNanos", System.nanoTime() - thesisStart);
        return splitSource;
    }
}
