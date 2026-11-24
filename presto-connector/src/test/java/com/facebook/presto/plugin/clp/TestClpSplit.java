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

import static com.facebook.presto.plugin.clp.ClpMetadata.DEFAULT_SCHEMA_NAME;
import static com.facebook.presto.plugin.clp.ClpMetadataDbSetUp.ARCHIVES_STORAGE_DIRECTORY_BASE;
import static com.facebook.presto.plugin.clp.ClpRuntimeMetrics.GET_SPLITS_NANOS;
import static com.facebook.presto.plugin.clp.ClpRuntimeMetrics.LIST_SPLITS_CONNECT_NANOS;
import static com.facebook.presto.plugin.clp.ClpRuntimeMetrics.LIST_SPLITS_FETCH_NANOS;
import static com.facebook.presto.plugin.clp.ClpRuntimeMetrics.LIST_SPLITS_NANOS;
import static com.facebook.presto.plugin.clp.ClpRuntimeMetrics.LIST_SPLITS_QUERY_NANOS;
import static com.facebook.presto.plugin.clp.ClpRuntimeMetrics.SPLIT_COUNT;
import static com.facebook.presto.testing.TestingSession.testSessionBuilder;
import static com.google.common.collect.ImmutableList.toImmutableList;
import static java.lang.String.format;
import static org.testng.Assert.assertEquals;
import static org.testng.Assert.assertTrue;

import java.util.List;
import java.util.Map;
import java.util.Optional;

import com.facebook.presto.common.RuntimeStats;
import com.facebook.presto.common.RuntimeUnit;
import com.facebook.presto.spi.ConnectorSession;
import com.facebook.presto.spi.SchemaTableName;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import org.testng.annotations.AfterMethod;
import org.testng.annotations.BeforeMethod;
import org.testng.annotations.Test;

import com.facebook.presto.plugin.clp.mockdb.ClpMockMetadataDatabase;
import com.facebook.presto.plugin.clp.mockdb.table.ArchivesTableRows;
import com.facebook.presto.plugin.clp.split.ClpMySqlSplitProvider;
import com.facebook.presto.plugin.clp.split.ClpSplitProvider;

@Test(singleThreaded = true)
public class TestClpSplit {
    private ClpMockMetadataDatabase mockMetadataDatabase;
    private ClpSplitProvider clpSplitProvider;
    private Map<String, ArchivesTableRows> tableSplits;

    @BeforeMethod
    public void setUp() {
        mockMetadataDatabase = ClpMockMetadataDatabase.builder().build();
        ImmutableList.Builder<String> tableNamesBuilder = ImmutableList.builder();
        ImmutableMap.Builder<String, ArchivesTableRows> splitsMapBuilder = ImmutableMap.builder();

        int numTables = 3;
        int numSplitsPerTable = 10;

        for (int i = 0; i < numTables; i++) {
            String tableName = "test_split_" + i;
            tableNamesBuilder.add(tableName);

            ImmutableList.Builder<String> idsBuilder = ImmutableList.builder();
            ImmutableList.Builder<Long> beginTimestampsBuilder = ImmutableList.builder();
            ImmutableList.Builder<Long> endTimestampsBuilder = ImmutableList.builder();
            for (int j = 0; j < numSplitsPerTable; j++) {
                // We generate synthetic begin_timestamp and end_timestamp values for each split
                // by offsetting two base timestamps (1700000000000L and 1705000000000L) with a
                // fixed increment per split (10^10 * j).
                idsBuilder.add(format("id_%s", j));
                beginTimestampsBuilder.add(1700000000000L + 10000000000L * j);
                endTimestampsBuilder.add(1705000000000L + 10000000000L * j);
            }
            splitsMapBuilder.put(
                    tableName,
                    new ArchivesTableRows(
                            idsBuilder.build(),
                            beginTimestampsBuilder.build(),
                            endTimestampsBuilder.build()
                    )
            );
        }

        mockMetadataDatabase.addTableToDatasetsTableIfNotExist(tableNamesBuilder.build());
        tableSplits = splitsMapBuilder.build();
        mockMetadataDatabase.addSplits(tableSplits);
        ClpConfig config = new ClpConfig().setPolymorphicTypeEnabled(true).setMetadataDbUrl(
                mockMetadataDatabase.getUrl()
        ).setMetadataDbUser(mockMetadataDatabase.getUsername()).setMetadataDbPassword(
                mockMetadataDatabase.getPassword()
        ).setMetadataTablePrefix(mockMetadataDatabase.getTablePrefix());
        clpSplitProvider = new ClpMySqlSplitProvider(config);
    }

    @AfterMethod
    public void tearDown() {
        if (null != mockMetadataDatabase) {
            mockMetadataDatabase.teardown();
        }
    }

    @Test
    public void testListSplits() {
        for (Map.Entry<String, ArchivesTableRows> entry : tableSplits.entrySet()) {
            // Without metadata filters
            compareListSplitsResult(
                    entry,
                    Optional.empty(),
                    ImmutableList.of(0, 1, 2, 3, 4, 5, 6, 7, 8, 9)
            );

            // query_begin_ts < archives_min_ts && query_end_ts > archives_max_ts
            compareListSplitsResult(
                    entry,
                    Optional.of(
                            "(end_timestamp > 1699999999999 AND begin_timestamp < 1795000000001)"
                    ),
                    ImmutableList.of(0, 1, 2, 3, 4, 5, 6, 7, 8, 9)
            );

            // query_begin_ts < archives_min_ts && query_end_ts > archives_min_ts && query_end_ts <
            // archives_max_ts
            compareListSplitsResult(
                    entry,
                    Optional.of(
                            "(end_timestamp > 1699999999999 AND begin_timestamp < 1744999999999)"
                    ),
                    ImmutableList.of(0, 1, 2, 3, 4)
            );

            // query_end_ts < archives_min_ts
            compareListSplitsResult(
                    entry,
                    Optional.of("(begin_timestamp < 1699999999999)"),
                    ImmutableList.of()
            );

            // query_begin_ts > archives_min_ts && query_begin_ts < archives_max_ts && query_end_ts
            // > archives_max_ts
            compareListSplitsResult(
                    entry,
                    Optional.of(
                            "(end_timestamp > 1745000000001 AND begin_timestamp < 1795000000001)"
                    ),
                    ImmutableList.of(5, 6, 7, 8, 9)
            );

            // query_begin_ts > archives_min_ts && query_begin_ts < archives_max_ts && query_end_ts
            // < archives_max_ts
            compareListSplitsResult(
                    entry,
                    Optional.of(
                            "(end_timestamp > 1745000000001 AND begin_timestamp <= 1770000000000)"
                    ),
                    ImmutableList.of(5, 6, 7)
            );

            // query_begin_ts > archives_max_ts
            compareListSplitsResult(
                    entry,
                    Optional.of("(end_timestamp > 1795000000001)"),
                    ImmutableList.of()
            );

            // query_begin_ts = archive_min_ts
            compareListSplitsResult(
                    entry,
                    Optional.of(
                            "(end_timestamp >= 1700000000000 AND begin_timestamp <= 1700000000000)"
                    ),
                    ImmutableList.of(0)
            );

            // query_begin_ts = archive_max_ts
            compareListSplitsResult(
                    entry,
                    Optional.of(
                            "(end_timestamp >= 1795000000000 AND begin_timestamp <= 1795000000000)"
                    ),
                    ImmutableList.of(9)
            );

            // query_ts = x && x > archive_min_ts && x < archive_max_ts (non-exist)
            compareListSplitsResult(
                    entry,
                    Optional.of(
                            "(end_timestamp >= 1715000000001 AND begin_timestamp <= 1715000000001)"
                    ),
                    ImmutableList.of()
            );

            // query_ts = x && x > archive_min_ts && x < archive_max_ts
            compareListSplitsResult(
                    entry,
                    Optional.of(
                            "(end_timestamp >= 1715000000000 AND begin_timestamp <= 1715000000000)"
                    ),
                    ImmutableList.of(1)
            );
        }
    }

    @Test
    public void testGetSplitsRecordsRuntimeMetrics() {
        ConnectorSession session = testSessionBuilder().build().toConnectorSession();
        String tableName = tableSplits.keySet().iterator().next();
        ClpTableLayoutHandle layoutHandle = new ClpTableLayoutHandle(
                new ClpTableHandle(
                        new SchemaTableName(DEFAULT_SCHEMA_NAME, tableName),
                        ARCHIVES_STORAGE_DIRECTORY_BASE + tableName
                ),
                Optional.empty(),
                Optional.empty()
        );
        new ClpSplitManager(clpSplitProvider).getSplits(null, session, layoutHandle, null).close();

        RuntimeStats stats = session.getRuntimeStats();
        assertEquals(stats.getMetric(SPLIT_COUNT).getSum(), 10);
        assertEquals(stats.getMetric(SPLIT_COUNT).getUnit(), RuntimeUnit.NONE);
        for (
            String name : ImmutableList.of(
                    GET_SPLITS_NANOS,
                    LIST_SPLITS_NANOS,
                    LIST_SPLITS_CONNECT_NANOS,
                    LIST_SPLITS_QUERY_NANOS,
                    LIST_SPLITS_FETCH_NANOS
            )
        ) {
            assertEquals(stats.getMetric(name).getCount(), 1, name);
            assertEquals(stats.getMetric(name).getUnit(), RuntimeUnit.NANO, name);
        }

        // The MySQL steps lie inside the listing, and the listing inside getSplits.
        long listSplitsNanos = stats.getMetric(LIST_SPLITS_NANOS).getSum();
        assertTrue(
                stats.getMetric(LIST_SPLITS_CONNECT_NANOS).getSum() + stats.getMetric(
                        LIST_SPLITS_QUERY_NANOS
                ).getSum() + stats.getMetric(LIST_SPLITS_FETCH_NANOS).getSum() <= listSplitsNanos
        );
        assertTrue(listSplitsNanos <= stats.getMetric(GET_SPLITS_NANOS).getSum());
    }

    private void compareListSplitsResult(
            Map.Entry<String, ArchivesTableRows> entry,
            Optional<String> metadataSql,
            List<Integer> expectedSplitIndexes
    ) {
        String tableName = entry.getKey();
        String tablePath = ARCHIVES_STORAGE_DIRECTORY_BASE + tableName;
        ClpTableLayoutHandle layoutHandle = new ClpTableLayoutHandle(
                new ClpTableHandle(new SchemaTableName(DEFAULT_SCHEMA_NAME, tableName), tablePath),
                Optional.empty(),
                metadataSql
        );
        List<String> expectedSplitPaths = expectedSplitIndexes.stream().map(
                expectedSplitIndex -> format(
                        "%s/%s",
                        tablePath,
                        entry.getValue().getIds().get(expectedSplitIndex)
                )
        ).collect(toImmutableList());
        List<ClpSplit> actualSplits = clpSplitProvider.listSplits(layoutHandle);
        assertEquals(actualSplits.size(), expectedSplitPaths.size());

        ImmutableList<String> actualSplitPaths = actualSplits.stream().map(ClpSplit::getPath)
                .collect(toImmutableList());

        assertEquals(actualSplitPaths, expectedSplitPaths);
    }
}
