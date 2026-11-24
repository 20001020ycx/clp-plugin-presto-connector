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

import static com.facebook.presto.common.type.BigintType.BIGINT;
import static com.facebook.presto.common.type.BooleanType.BOOLEAN;
import static com.facebook.presto.common.type.DoubleType.DOUBLE;
import static com.facebook.presto.common.type.VarcharType.VARCHAR;
import static com.facebook.presto.plugin.clp.ClpMetadata.DEFAULT_SCHEMA_NAME;
import static com.facebook.presto.plugin.clp.ClpRuntimeMetrics.COLUMN_HANDLE_CACHE_HITS;
import static com.facebook.presto.plugin.clp.ClpRuntimeMetrics.COLUMN_HANDLE_CACHE_LOAD_NANOS;
import static com.facebook.presto.plugin.clp.ClpRuntimeMetrics.COLUMN_HANDLE_CACHE_LOOKUP_NANOS;
import static com.facebook.presto.plugin.clp.ClpRuntimeMetrics.COLUMN_HANDLE_CACHE_MISSES;
import static com.facebook.presto.plugin.clp.ClpRuntimeMetrics.METADATA_GET_COLUMN_HANDLES_NANOS;
import static com.facebook.presto.plugin.clp.ClpRuntimeMetrics.METADATA_GET_TABLE_HANDLE_NANOS;
import static com.facebook.presto.plugin.clp.ClpRuntimeMetrics.METADATA_GET_TABLE_METADATA_NANOS;
import static com.facebook.presto.plugin.clp.ClpRuntimeMetrics.TABLE_HANDLE_CACHE_HITS;
import static com.facebook.presto.plugin.clp.ClpRuntimeMetrics.TABLE_HANDLE_CACHE_LOAD_NANOS;
import static com.facebook.presto.plugin.clp.ClpRuntimeMetrics.TABLE_HANDLE_CACHE_LOOKUP_NANOS;
import static com.facebook.presto.plugin.clp.ClpRuntimeMetrics.TABLE_HANDLE_CACHE_MISSES;
import static com.facebook.presto.plugin.clp.metadata.ClpSchemaTreeNodeType.Boolean;
import static com.facebook.presto.plugin.clp.metadata.ClpSchemaTreeNodeType.ClpString;
import static com.facebook.presto.plugin.clp.metadata.ClpSchemaTreeNodeType.Float;
import static com.facebook.presto.plugin.clp.metadata.ClpSchemaTreeNodeType.Integer;
import static com.facebook.presto.plugin.clp.metadata.ClpSchemaTreeNodeType.UnstructuredArray;
import static com.facebook.presto.plugin.clp.metadata.ClpSchemaTreeNodeType.VarString;
import static com.facebook.presto.testing.TestingConnectorSession.SESSION;
import static com.facebook.presto.testing.TestingSession.testSessionBuilder;
import static org.testng.Assert.assertEquals;
import static org.testng.Assert.assertTrue;

import java.util.HashSet;
import java.util.Optional;

import com.facebook.presto.common.RuntimeStats;
import com.facebook.presto.common.RuntimeUnit;
import com.facebook.presto.common.type.ArrayType;
import com.facebook.presto.common.type.RowType;
import com.facebook.presto.spi.ColumnMetadata;
import com.facebook.presto.spi.ConnectorSession;
import com.facebook.presto.spi.ConnectorTableHandle;
import com.facebook.presto.spi.ConnectorTableMetadata;
import com.facebook.presto.spi.SchemaTableName;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.google.common.collect.ImmutableSet;
import org.testng.annotations.AfterMethod;
import org.testng.annotations.BeforeMethod;
import org.testng.annotations.Test;

import com.facebook.presto.plugin.clp.metadata.ClpMetadataProvider;
import com.facebook.presto.plugin.clp.metadata.ClpMySqlMetadataProvider;
import com.facebook.presto.plugin.clp.mockdb.ClpMockMetadataDatabase;
import com.facebook.presto.plugin.clp.mockdb.table.ColumnMetadataTableRows;

@Test(singleThreaded = true)
public class TestClpMetadata {
    private static final String TABLE_NAME = "test_metadata";
    private ClpMockMetadataDatabase mockMetadataDatabase;
    private ClpMetadata metadata;

    @BeforeMethod
    public void setUp() {
        mockMetadataDatabase = ClpMockMetadataDatabase.builder().build();
        mockMetadataDatabase.addTableToDatasetsTableIfNotExist(ImmutableList.of(TABLE_NAME));
        mockMetadataDatabase.addColumnMetadata(
                ImmutableMap.of(
                        TABLE_NAME,
                        new ColumnMetadataTableRows(
                                ImmutableList.of("a", "a", "b", "b", "c.d", "c.e", "f.g.h"),
                                ImmutableList.of(
                                        Integer,
                                        ClpString,
                                        Float,
                                        ClpString,
                                        Boolean,
                                        VarString,
                                        UnstructuredArray
                                )
                        )
                )
        );
        ClpConfig config = new ClpConfig().setPolymorphicTypeEnabled(true).setMetadataDbUrl(
                mockMetadataDatabase.getUrl()
        ).setMetadataDbUser(mockMetadataDatabase.getUsername()).setMetadataDbPassword(
                mockMetadataDatabase.getPassword()
        ).setMetadataTablePrefix(mockMetadataDatabase.getTablePrefix());
        ClpMetadataProvider metadataProvider = new ClpMySqlMetadataProvider(config);
        metadata = new ClpMetadata(config, metadataProvider);
    }

    @AfterMethod
    public void tearDown() {
        if (null != mockMetadataDatabase) {
            mockMetadataDatabase.teardown();
        }
    }

    @Test
    public void testListSchemaNames() {
        assertEquals(metadata.listSchemaNames(SESSION), ImmutableList.of(DEFAULT_SCHEMA_NAME));
    }

    @Test
    public void testRecordsRuntimeMetrics() {
        ConnectorSession session = testSessionBuilder().build().toConnectorSession();
        SchemaTableName tableName = new SchemaTableName(DEFAULT_SCHEMA_NAME, TABLE_NAME);
        ConnectorTableHandle tableHandle = metadata.getTableHandle(session, tableName);
        metadata.getTableHandle(session, tableName);
        metadata.getColumnHandles(session, tableHandle);
        metadata.getTableMetadata(session, tableHandle);

        RuntimeStats stats = session.getRuntimeStats();
        assertEquals(stats.getMetric(METADATA_GET_TABLE_HANDLE_NANOS).getCount(), 2);
        assertEquals(stats.getMetric(METADATA_GET_COLUMN_HANDLES_NANOS).getCount(), 1);
        assertEquals(stats.getMetric(METADATA_GET_TABLE_METADATA_NANOS).getCount(), 1);
        assertEquals(stats.getMetric(METADATA_GET_TABLE_HANDLE_NANOS).getUnit(), RuntimeUnit.NANO);

        // The first lookup in each cache runs the loader; the second is served from the cache.
        assertEquals(stats.getMetric(TABLE_HANDLE_CACHE_MISSES).getSum(), 1);
        assertEquals(stats.getMetric(TABLE_HANDLE_CACHE_HITS).getSum(), 1);
        assertEquals(stats.getMetric(TABLE_HANDLE_CACHE_LOOKUP_NANOS).getCount(), 2);
        assertEquals(stats.getMetric(TABLE_HANDLE_CACHE_LOAD_NANOS).getCount(), 1);
        assertEquals(stats.getMetric(COLUMN_HANDLE_CACHE_MISSES).getSum(), 1);
        assertEquals(stats.getMetric(COLUMN_HANDLE_CACHE_HITS).getSum(), 1);
        assertEquals(stats.getMetric(COLUMN_HANDLE_CACHE_LOOKUP_NANOS).getCount(), 2);
        assertEquals(stats.getMetric(COLUMN_HANDLE_CACHE_LOAD_NANOS).getCount(), 1);

        // Each timer lies inside the one around it.
        assertTrue(
                stats.getMetric(TABLE_HANDLE_CACHE_LOAD_NANOS).getSum() <= stats.getMetric(
                        TABLE_HANDLE_CACHE_LOOKUP_NANOS
                ).getSum()
        );
        assertTrue(
                stats.getMetric(TABLE_HANDLE_CACHE_LOOKUP_NANOS).getSum() <= stats.getMetric(
                        METADATA_GET_TABLE_HANDLE_NANOS
                ).getSum()
        );
    }

    @Test
    public void testListTables() {
        ImmutableSet.Builder<SchemaTableName> builder = ImmutableSet.builder();
        builder.add(new SchemaTableName(DEFAULT_SCHEMA_NAME, TABLE_NAME));
        assertEquals(
                new HashSet<>(metadata.listTables(SESSION, Optional.empty())),
                builder.build()
        );
    }

    @Test
    public void testGetTableMetadata() {
        ClpTableHandle clpTableHandle = (ClpTableHandle)metadata.getTableHandle(
                SESSION,
                new SchemaTableName(DEFAULT_SCHEMA_NAME, TABLE_NAME)
        );
        ConnectorTableMetadata tableMetadata = metadata.getTableMetadata(SESSION, clpTableHandle);
        ImmutableSet<ColumnMetadata> columnMetadata = ImmutableSet.<ColumnMetadata>builder().add(
                ColumnMetadata.builder().setName("a_bigint").setType(BIGINT).setNullable(true)
                        .build()
        ).add(
                ColumnMetadata.builder().setName("a_varchar").setType(VARCHAR).setNullable(true)
                        .build()
        ).add(
                ColumnMetadata.builder().setName("b_double").setType(DOUBLE).setNullable(true)
                        .build()
        ).add(
                ColumnMetadata.builder().setName("b_varchar").setType(VARCHAR).setNullable(true)
                        .build()
        ).add(
                ColumnMetadata.builder().setName("c").setType(
                        RowType.from(
                                ImmutableList.of(
                                        RowType.field("d", BOOLEAN),
                                        RowType.field("e", VARCHAR)
                                )
                        )
                ).setNullable(true).build()
        ).add(
                ColumnMetadata.builder().setName("f").setType(
                        RowType.from(
                                ImmutableList.of(
                                        RowType.field(
                                                "g",
                                                RowType.from(
                                                        ImmutableList.of(
                                                                RowType.field(
                                                                        "h",
                                                                        new ArrayType(VARCHAR)
                                                                )
                                                        )
                                                )
                                        )
                                )
                        )
                ).setNullable(true).build()
        ).build();
        assertEquals(columnMetadata, ImmutableSet.copyOf(tableMetadata.getColumns()));
    }
}
