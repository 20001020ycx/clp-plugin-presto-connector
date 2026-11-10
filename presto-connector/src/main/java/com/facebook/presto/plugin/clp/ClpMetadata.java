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

import com.facebook.presto.plugin.clp.metadata.ClpMetadataProvider;
import com.facebook.presto.spi.ColumnHandle;
import com.facebook.presto.spi.ColumnMetadata;
import com.facebook.presto.spi.ConnectorSession;
import com.facebook.presto.spi.ConnectorTableHandle;
import com.facebook.presto.spi.ConnectorTableLayout;
import com.facebook.presto.spi.ConnectorTableLayoutHandle;
import com.facebook.presto.spi.ConnectorTableLayoutResult;
import com.facebook.presto.spi.ConnectorTableMetadata;
import com.facebook.presto.spi.Constraint;
import com.facebook.presto.spi.SchemaTableName;
import com.facebook.presto.spi.SchemaTablePrefix;
import com.facebook.presto.spi.connector.ConnectorMetadata;
import com.google.common.cache.CacheBuilder;
import com.google.common.cache.CacheLoader;
import com.google.common.cache.LoadingCache;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;

import javax.inject.Inject;

import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.function.Function;

import static com.google.common.collect.ImmutableList.toImmutableList;
import static com.google.common.collect.ImmutableMap.toImmutableMap;
import static java.util.Objects.requireNonNull;
import static java.util.concurrent.TimeUnit.SECONDS;

/**
 * For efficiency, this class maintains two caches for metadata from the
 * {@link ClpMetadataProvider}:
 * <ul>
 *     <li>columnHandleCache: Maps {@link SchemaTableName} to a list of {@link ClpColumnHandle}s.
 *     </li>
 *     <li>tableHandleCache: Maps schema names to a list of {@link ClpTableHandle}s.</li>
 * </ul>
 */
public class ClpMetadata
        implements ConnectorMetadata
{
    public static final String DEFAULT_SCHEMA_NAME = "default";

    private final ClpMetadataProvider clpMetadataProvider;
    private final LoadingCache<SchemaTableName, List<ClpColumnHandle>> columnHandleCache;
    private final LoadingCache<String, List<ClpTableHandle>> tableHandleCache;

    @Inject
    public ClpMetadata(ClpConfig clpConfig, ClpMetadataProvider clpMetadataProvider)
    {
        this.columnHandleCache = CacheBuilder.newBuilder()
                .expireAfterWrite(clpConfig.getMetadataExpireInterval(), SECONDS)
                .refreshAfterWrite(clpConfig.getMetadataRefreshInterval(), SECONDS)
                .build(CacheLoader.from(this::loadColumnHandles));
        this.tableHandleCache = CacheBuilder.newBuilder()
                .expireAfterWrite(clpConfig.getMetadataExpireInterval(), SECONDS)
                .refreshAfterWrite(clpConfig.getMetadataRefreshInterval(), SECONDS)
                .build(CacheLoader.from(this::loadTableHandles));

        this.clpMetadataProvider = requireNonNull(clpMetadataProvider, "ClpMetadataProvider is null");
    }

    @Override
    public List<String> listSchemaNames(ConnectorSession session)
    {
        return ImmutableList.of(DEFAULT_SCHEMA_NAME);
    }

    @Override
    public List<SchemaTableName> listTables(ConnectorSession session, Optional<String> schemaName)
    {
        long thesisStart = System.nanoTime();
        try {
            String schemaNameValue = schemaName.orElse(DEFAULT_SCHEMA_NAME);
            if (!listSchemaNames(session).contains(schemaNameValue)) {
                return ImmutableList.of();
            }

            return listTableHandles(session, schemaNameValue).stream()
                    .map(tableHandle -> new SchemaTableName(schemaNameValue, tableHandle.getSchemaTableName().getTableName()))
                    .collect(toImmutableList());
        }
        finally {
            ClpThesisMetrics.addNanos(session, "clpMetadataListTablesNanos", System.nanoTime() - thesisStart);
        }
    }

    @Override
    public ConnectorTableHandle getTableHandle(ConnectorSession session, SchemaTableName tableName)
    {
        long thesisStart = System.nanoTime();
        try {
            String schemaName = tableName.getSchemaName();
            if (!listSchemaNames(session).contains(schemaName)) {
                return null;
            }

            return listTableHandles(session, schemaName).stream()
                    .filter(tableHandle -> tableHandle.getSchemaTableName().equals(tableName))
                    .findFirst()
                    .orElse(null);
        }
        finally {
            ClpThesisMetrics.addNanos(session, "clpMetadataGetTableHandleNanos", System.nanoTime() - thesisStart);
        }
    }

    @Override
    public ConnectorTableLayoutResult getTableLayoutForConstraint(
            ConnectorSession session,
            ConnectorTableHandle table,
            Constraint<ColumnHandle> constraint,
            Optional<Set<ColumnHandle>> desiredColumns)
    {
        ClpTableHandle tableHandle = (ClpTableHandle) table;
        ConnectorTableLayout layout = new ConnectorTableLayout(new ClpTableLayoutHandle(tableHandle, Optional.empty(), Optional.empty()));
        return new ConnectorTableLayoutResult(layout, constraint.getSummary());
    }

    @Override
    public ConnectorTableLayout getTableLayout(ConnectorSession session, ConnectorTableLayoutHandle handle)
    {
        return new ConnectorTableLayout(handle);
    }

    @Override
    public ConnectorTableMetadata getTableMetadata(ConnectorSession session, ConnectorTableHandle table)
    {
        long thesisStart = System.nanoTime();
        try {
            ClpTableHandle clpTableHandle = (ClpTableHandle) table;
            SchemaTableName schemaTableName = clpTableHandle.getSchemaTableName();
            List<ColumnMetadata> columns = listColumns(session, schemaTableName).stream()
                    .map(ClpColumnHandle::getColumnMetadata)
                    .collect(toImmutableList());

            return new ConnectorTableMetadata(schemaTableName, columns);
        }
        finally {
            ClpThesisMetrics.addNanos(session, "clpMetadataGetTableMetadataNanos", System.nanoTime() - thesisStart);
        }
    }

    @Override
    public Map<SchemaTableName, List<ColumnMetadata>> listTableColumns(ConnectorSession session, SchemaTablePrefix prefix)
    {
        // thesis-metrics: this duration contains those of the calls it makes below.
        long thesisStart = System.nanoTime();
        try {
            return listTableColumnsImpl(session, prefix);
        }
        finally {
            ClpThesisMetrics.addNanos(session, "clpMetadataListTableColumnsNanos", System.nanoTime() - thesisStart);
        }
    }

    private Map<SchemaTableName, List<ColumnMetadata>> listTableColumnsImpl(ConnectorSession session, SchemaTablePrefix prefix)
    {
        requireNonNull(prefix, "prefix is null");
        String schemaName = prefix.getSchemaName();
        if (schemaName != null && !listSchemaNames(session).contains(schemaName)) {
            return ImmutableMap.of();
        }

        List<SchemaTableName> schemaTableNames;
        if (prefix.getTableName() == null) {
            schemaTableNames = listTables(session, Optional.ofNullable(prefix.getSchemaName()));
        }
        else {
            SchemaTableName table = new SchemaTableName(schemaName, prefix.getTableName());
            if (listTables(session, Optional.ofNullable(schemaName)).contains(table)) {
                schemaTableNames = ImmutableList.of(table);
            }
            else {
                schemaTableNames = ImmutableList.of();
            }
        }

        return schemaTableNames.stream()
                .collect(toImmutableMap(
                        Function.identity(),
                        tableName -> getTableMetadata(session, getTableHandle(session, tableName)).getColumns()));
    }

    @Override
    public Map<String, ColumnHandle> getColumnHandles(ConnectorSession session, ConnectorTableHandle tableHandle)
    {
        long thesisStart = System.nanoTime();
        try {
            ClpTableHandle clpTableHandle = (ClpTableHandle) tableHandle;
            return listColumns(session, clpTableHandle.getSchemaTableName()).stream().collect(toImmutableMap(ClpColumnHandle::getColumnName, column -> column));
        }
        finally {
            ClpThesisMetrics.addNanos(session, "clpMetadataGetColumnHandlesNanos", System.nanoTime() - thesisStart);
        }
    }

    @Override
    public ColumnMetadata getColumnMetadata(ConnectorSession session, ConnectorTableHandle tableHandle, ColumnHandle columnHandle)
    {
        ClpColumnHandle clpColumnHandle = (ClpColumnHandle) columnHandle;
        return clpColumnHandle.getColumnMetadata();
    }

    // thesis-metrics: the loaders run on the thread of the lookup that misses (or that refreshes
    // an entry), so the lookup below finds the loader's duration pending and counts a miss.
    private List<ClpColumnHandle> loadColumnHandles(SchemaTableName schemaTableName)
    {
        long thesisStart = System.nanoTime();
        try {
            return clpMetadataProvider.listColumnHandles(schemaTableName);
        }
        finally {
            ClpThesisMetrics.pendingNanos("clpColumnHandleCacheLoadNanos", System.nanoTime() - thesisStart);
        }
    }

    private List<ClpTableHandle> loadTableHandles(String schemaName)
    {
        long thesisStart = System.nanoTime();
        try {
            return clpMetadataProvider.listTableHandles(schemaName);
        }
        finally {
            ClpThesisMetrics.pendingNanos("clpTableHandleCacheLoadNanos", System.nanoTime() - thesisStart);
        }
    }

    private List<ClpColumnHandle> listColumns(ConnectorSession session, SchemaTableName schemaTableName)
    {
        ClpThesisMetrics.clearPending();
        long thesisStart = System.nanoTime();
        try {
            return columnHandleCache.getUnchecked(schemaTableName);
        }
        finally {
            ClpThesisMetrics.addNanos(session, "clpColumnHandleCacheLookupNanos", System.nanoTime() - thesisStart);
            boolean loaded = ClpThesisMetrics.flushPending(session);
            ClpThesisMetrics.addCount(session, loaded ? "clpColumnHandleCacheMisses" : "clpColumnHandleCacheHits", 1);
        }
    }

    private List<ClpTableHandle> listTableHandles(ConnectorSession session, String schemaName)
    {
        ClpThesisMetrics.clearPending();
        long thesisStart = System.nanoTime();
        try {
            return tableHandleCache.getUnchecked(schemaName);
        }
        finally {
            ClpThesisMetrics.addNanos(session, "clpTableHandleCacheLookupNanos", System.nanoTime() - thesisStart);
            boolean loaded = ClpThesisMetrics.flushPending(session);
            ClpThesisMetrics.addCount(session, loaded ? "clpTableHandleCacheMisses" : "clpTableHandleCacheHits", 1);
        }
    }

    // Presto 0.297 added normalizeIdentifier() to MetadataManager.getColumnHandles(), which
    // lowercases column names by default. CLP schema field names are case-sensitive, so we
    // override this to return the identifier unchanged and prevent the case mismatch between
    // getColumnHandles() and getTableMetadata().getColumns().
    @Override
    public String normalizeIdentifier(ConnectorSession session, String identifier)
    {
        return identifier;
    }
}
