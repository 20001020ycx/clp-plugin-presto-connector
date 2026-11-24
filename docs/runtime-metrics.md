# Runtime metrics

The connector reports where a query's time goes inside it: on each worker, what the scan did with
each split; on the coordinator, its metadata lookups, pushdown and split listing. The values are
Presto runtime metrics, so they appear in the query info (`GET /v1/query/{queryId}`) of every query,
with no setting to turn on.

* **Worker metrics** belong to each TableScan. The query's `queryStats.runtimeStats` has them as
  `S<stageId>-TableScan.<planNodeId>.<metric>`, summed over the scan's drivers and workers; each
  stage's TableScan operator summary
  (`…latestAttemptExecutionInfo.stats.operatorSummaries[].runtimeStats`) has them as
  `TableScan.<planNodeId>.<metric>`.
* **Coordinator metrics** are in `queryStats.runtimeStats` under their own names.

Each value is a Presto `RuntimeMetric`: `sum`, `count`, `min` and `max`. For a worker metric,
`sum` is the total over the splits, while `count`, `min` and `max` describe the per-split values:
`count` is the number of splits in which the phase ran. A coordinator metric has one value per call.

Units are nanoseconds (`…Nanos`), bytes (`…Bytes…`) or plain counts. Wall time comes from a
monotonic clock; CPU time is the calling thread's CPU clock (`CLOCK_THREAD_CPUTIME_ID`).

## Worker: phases of the scan

Each phase reports its wall time (`…WallNanos`) and CPU time (`…CpuNanos`). The phases that can read
an archive also report `…StorageWaitNanos`: the part of their wall time spent inside the archive's
reader, which is where the scan blocks on object storage or the disk (see
[Storage](#worker-storage)). The phases that run more than once per split also report `…Calls`.

Wall time minus CPU time is the time the scan's thread was off the CPU, which is how long it waited
for storage, or for a core when the worker has more drivers than cores.

**The outer phases** do not overlap, and together they cover every call that the TableScan makes into
the data source. Their sum is the TableScan's time in the connector:

| Metrics | Function measured | Meaning |
|---|---|---|
| `clpAddSplitWallNanos`, `clpAddSplitCpuNanos` | `ClpDataSource::addSplit` | Creating the split's cursor, signing its URL, and preparing the query (includes `clpParseQuery…`). |
| `clpNextWallNanos`, `clpNextCpuNanos`, `clpNextStorageWaitNanos`, `clpNextCalls` | `ClpDataSource::next` | Every call that produces a batch, the split's loading included. Excludes closing the archive. |
| `clpCloseArchiveWallNanos`, `clpCloseArchiveCpuNanos` | `ClpDataSource::closeCursor` | Destroying the split's cursor when its last batch is done, which closes the archive and its reader. For an archive in object storage that aborts the rest of the download and waits for the download thread to stop (see `clpStorageCloseNanos`). |

**Inside `clpAddSplit…`:**

| Metrics | Function measured | Meaning |
|---|---|---|
| `clpParseQueryWallNanos`, `clpParseQueryCpuNanos` | `BaseClpCursor::executeQuery` | Parsing the KQL and normalizing it. |

**Inside `clpNext…`:** loading the split once, then a loop over the matched schemas' tables.

| Metrics | Function measured | Meaning |
|---|---|---|
| `clpLoadSplitWallNanos`, `clpLoadSplitCpuNanos`, `clpLoadSplitStorageWaitNanos` | `ClpArchiveCursor::loadSplit` | Once per split; the sum of the seven phases below, plus early returns. |
| `clpSchemaInitWallNanos`, `clpSchemaInitCpuNanos`, `clpSchemaInitCalls` | `ClpQueryRunner::schema_init` | Specializing the filter for each matched schema. |
| `clpReadTableWallNanos`, `clpReadTableCpuNanos`, `clpReadTableStorageWaitNanos`, `clpReadTableCalls` | `ArchiveReader::read_schema_table` | Reading a table's packed stream and decompressing it. |
| `clpFilterInitWallNanos`, `clpFilterInitCpuNanos`, `clpFilterInitCalls` | `SchemaReader::initialize_filter_with_column_map` | Binding the filter to the table's columns. |
| `clpFilterWallNanos`, `clpFilterCpuNanos`, `clpFilterCalls` | `ClpQueryRunner::fetchNext` | Evaluating the filter on the table's records. |
| `clpCreateVectorWallNanos`, `clpCreateVectorCpuNanos`, `clpCreateVectorCalls` | `ClpArchiveCursor::createVector` | Building the batch's row vector of lazy vectors. |

**Inside `clpLoadSplit…`,** in the order they run:

| Metrics | Function measured | Meaning |
|---|---|---|
| `clpOpenArchiveWallNanos`, `clpOpenArchiveCpuNanos`, `clpOpenArchiveStorageWaitNanos` | `ArchiveReader::open` | Opening the archive: its header, metadata, timestamp index, schema tree and schema map. |
| `clpTimestampIndexAndSchemaMatchWallNanos`, `clpTimestampIndexAndSchemaMatchCpuNanos` | `EvaluateTimestampIndex`, `SchemaMatch` | Pruning by the timestamp index and matching the query against the schemas (two pieces of code). |
| `clpProjectionWallNanos`, `clpProjectionCpuNanos` | `Projection::resolve_columns` | Resolving the projected columns. |
| `clpReadTableMetadataWallNanos`, `clpReadTableMetadataCpuNanos`, `clpReadTableMetadataStorageWaitNanos` | `ArchiveReader::read_metadata` | Reading the tables' metadata. |
| `clpLoadDictionariesWallNanos`, `clpLoadDictionariesCpuNanos`, `clpLoadDictionariesStorageWaitNanos` | `ArchiveReader::read_*_dictionary` | Loading the variable, log-type and array dictionaries. |
| `clpOpenPackedStreamsWallNanos`, `clpOpenPackedStreamsCpuNanos`, `clpOpenPackedStreamsStorageWaitNanos` | `ArchiveReader::open_packed_streams` | Opening the packed streams that hold the tables. |
| `clpQueryRunnerInitWallNanos`, `clpQueryRunnerInitCpuNanos` | `ClpQueryRunner::global_init` | Creating the query runner. |

**Lazy loads** run when an operator downstream first reads a column of a batch, outside the outer
phases:

| Metrics | Function measured | Meaning |
|---|---|---|
| `clpLazyLoadJsonWallNanos`, `clpLazyLoadJsonCpuNanos`, `clpLazyLoadJsonCalls` | `ClpArchiveJsonStringVectorLoader::loadInternal` | Rendering records as JSON strings. |
| `clpLazyLoadColumnWallNanos`, `clpLazyLoadColumnCpuNanos`, `clpLazyLoadColumnCalls` | `ClpArchiveVectorLoader::loadInternal` | Materializing a typed column. |

The phases inside `clpNext…` and `clpLoadSplit…` cover archive splits. An IR split reports only the
outer phases.

## Worker: counters

| Metric | Meaning |
|---|---|
| `clpSplits` | Splits the scan processed (one per split). |
| `clpSchemasInArchive` | Schemas in the split's archive. |
| `clpSchemasMatched` | Schemas that can match the query. |
| `clpTablesRead` | Tables read and decompressed. |
| `clpRowsScanned` | Records the filter was evaluated on. |
| `clpRowsMatched` | Records that matched. |
| `clpJsonRows` | Records rendered as JSON strings. |
| `clpJsonBytes` | Bytes of those JSON strings. |
| `clpValuesMaterialized` | Values materialized into typed columns. |

## Worker: storage

CLP opens one reader per archive (one HTTP GET for an archive in object storage). The plugin counts
what the scan does through it by handing CLP a wrapper around its reader
(`velox-connector/src/connector/search_lib/ClpCountingReader.cpp`); the wrapper forwards every call
and changes nothing about what is read.

| Metric | Meaning |
|---|---|
| `clpStorageReadersOpened` | Readers opened: requests to object storage, or files opened. |
| `clpStorageReadersClosed` | Readers closed. |
| `clpStorageCalls` | Reads and seeks on the readers. |
| `clpStorageWaitNanos` | Wall time inside those calls: waiting for data, plus copying it out of the reader's buffer. |
| `clpStorageBytesRead` | Bytes the reads returned. |
| `clpStorageBytesConsumed` | How far into the archives the reads and seeks went. A network reader fetches every byte up to that point, so this is the data the scan took from storage. It is also the TableScan's raw input bytes (`rawInputDataSizeInBytes` in its operator summary). |
| `clpStorageCloseNanos` | Wall time destroying the readers, inside `clpCloseArchive…`. For a network reader this includes aborting the download and waiting for its download thread to stop. |

What the plugin cannot see without a change in CLP: the bytes the download thread received, which can
exceed `clpStorageBytesConsumed` by its read-ahead when an archive is not read to the end, and the
download thread's own CPU and wall time. The object store's own counter of bytes sent shows the
former.

## Coordinator

Every value is added to the query's `queryStats.runtimeStats` once per call.

| Metric | Function measured | Meaning |
|---|---|---|
| `clpMetadataGetTableHandleNanos` | `ClpMetadata.getTableHandle` | Resolving the table. |
| `clpMetadataGetTableMetadataNanos` | `ClpMetadata.getTableMetadata` | Listing the table's columns. |
| `clpMetadataGetColumnHandlesNanos` | `ClpMetadata.getColumnHandles` | Building the table's column handles. |
| `clpMetadataListTablesNanos` | `ClpMetadata.listTables` | Listing the tables. |
| `clpMetadataListTableColumnsNanos` | `ClpMetadata.listTableColumns` | Listing tables' columns; includes the calls it makes to the methods above. |
| `clpTableHandleCacheLookupNanos` | `ClpMetadata.listTableHandles` | Looking up the schema's tables in the cache, a load included. |
| `clpTableHandleCacheHits`, `clpTableHandleCacheMisses` | | Lookups served from the cache, and lookups that ran the loader on their own thread (a miss or a refresh). |
| `clpTableHandleCacheLoadNanos` | `ClpMetadata.loadTableHandles` | The loader: the metadata database's query for the tables. |
| `clpColumnHandleCacheLookupNanos` | `ClpMetadata.listColumnHandles` | Looking up a table's columns in the cache, a load included. |
| `clpColumnHandleCacheHits`, `clpColumnHandleCacheMisses` | | As for the table handles. |
| `clpColumnHandleCacheLoadNanos` | `ClpMetadata.loadColumnHandles` | The loader: the metadata database's query for the columns. |
| `clpUdfRewriterNanos` | `ClpUdfRewriter.optimize` | Rewriting `CLP_GET_*` calls into column references. |
| `clpComputePushDownNanos` | `ClpComputePushDown.optimize` | Pushing filters down: the KQL and the metadata SQL. |
| `clpGetSplitsNanos` | `ClpSplitManager.getSplits` | Listing the splits and building the split source. |
| `clpListSplitsNanos` | `ClpSplitProvider.listSplits` | Listing the splits, inside `clpGetSplitsNanos`. |
| `clpSplitCount` | | Splits listed. Equals the sum of `clpSplits` over the query's TableScans once they finish. |
| `clpListSplitsConnectNanos` | `ClpMySqlSplitProvider.listSplits` | MySQL: opening the connection, inside `clpListSplitsNanos`. |
| `clpListSplitsQueryNanos` | `ClpMySqlSplitProvider.listSplits` | MySQL: running the query for the archives. |
| `clpListSplitsFetchNanos` | `ClpMySqlSplitProvider.listSplits` | MySQL: reading its rows into splits. |

## When the values are taken

The TableScan copies the worker's metrics when a split ends and when the scan closes; each copy
replaces the previous one, and the connector reports running totals, so each value is counted once.
The connector closes a split's archive as soon as its last batch is done, so the copy at the end of a
split includes the close, the last split's too.

One case remains outside the query info: a split that the scan abandons before its end, for example
because the query had all the rows it needed. Its archive is closed when the scan's data source is
destroyed, after the last copy. The per-split log below records that close.

## Per-split log

With `clp.log-split-stats=true` in a worker's catalog properties, the worker writes one line per
split to its log, with the split's nonzero values:

```text
CLP split stats {"scan":"<taskId>/<planNodeId>/<driverId>","path":"<split path>","metrics":{"clpAddSplitWallNanos":…}}
```

It is off by default, as it costs a log line per split.
