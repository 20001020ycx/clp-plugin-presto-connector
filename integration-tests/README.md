# Integration tests

Integration tests for the CLP Presto connector, run against a real two-node cluster: a Java
coordinator and a native (C++) worker, both loading the connector plugin.

## Why a real cluster

The worker plugin leaves Velox symbols undefined so that the worker process resolves them at load
time, which means that a test binary has nothing to link against. Running queries through a real
worker exercises the plugin without building Velox into the toolchain image.

## Running the tests using task

```shell
task integration-tests:run
```

That task first builds the init-container image that the cluster installs the plugins from. It
then brings the cluster up, runs the tests, and tears the cluster down again, whether or not the
tests passed.

The tests themselves run in a container on the cluster's network, because a published port is
reachable only from a process that shares a network namespace with the Docker daemon, which is not
the case on a containerized CI runner.

Arguments after `--` go to pytest, as in `task integration-tests:run -- -m ir`. The markers are
`archive`, `ir`, `pushdown`, `schema`, and `udf`.

The steps are also available separately, which is what CI uses so that it can capture the
cluster's logs before stopping it:

```shell
task integration-tests:up
task integration-tests:test
task integration-tests:down
```

## Running the tests from the host

While the cluster is up, the coordinator is published on the host, so the tests can also be run
directly, which is easier to attach a debugger to:

```shell
task integration-tests:up
cd integration-tests && uv run pytest -m schema
```

Any Presto client can reach it the same way:

```shell
presto-cli --server localhost:18080 --catalog clp --schema default --execute "SHOW TABLES"
```

## Configuration

Four variables override the defaults, on both the task path and a manual `docker compose` run:

| Variable | Default | What it changes |
| --- | --- | --- |
| `PRESTO_VERSION` | `0.299` | Tag of both server images |
| `CLP_PLUGIN_INSTALLER_IMAGE` | `ghcr.io/y-scope/clp-plugin-presto-connector:0.1.0-SNAPSHOT` | Installer image produced by `task package` |
| `CLP_INTEGRATION_TEST_FIXTURE_DIR` | `./fixtures` | Fixture tree mounted by the cluster |
| `CLP_INTEGRATION_TEST_COORDINATOR_PORT` | `18080` | Host port that the coordinator is published on |

Only the coordinator's port is published, and it defaults to 18080 rather than 8080 to stay clear
of whatever else is running. The ports inside the containers are fixed and cannot collide with the
host. If 18080 is taken, `docker compose up` fails with a bind error; set the variable and retry.

## Running the cluster without the tests

Useful when the compose file itself is what you are debugging.

1. Build the installer image that the compose file uses by default:

   ```shell
   task package
   ```

2. Bring the cluster up from this directory:

   ```shell
   docker compose up
   ```

   The worker waits for the coordinator to report healthy, so a clean `up` ends with both nodes
   running and the worker counted in `activeWorkers`.

Tear it down with `docker compose down -v`. The `-v` matters: the plugin directories are volumes,
so without it the next `up` reuses the previously installed plugin rather than the one you just
built. The task path already passes `--volumes`, so this only applies here.

## Fixtures

`fixtures/` is mounted into the cluster as-is; nothing generates it. A table is a **directory** of
archives, mirroring how CLP stores a dataset and how `ClpMySqlSplitProvider` addresses it
(`<tablePath>/<archiveId>`), so a directory holding several archives fans out to several splits.

Each directory also holds the `.ndjson` records its archives were compressed from. Nothing reads
them -- the connector only picks up `.clps` and `.clp.zst` -- but they are how you derive an
expected value without decompressing anything.

To add a table, add a directory. To add a split to one, drop in another archive.

A table may carry a `schema.json` listing `{"name", "type"}` entries, one per (field, storage type)
pair, mirroring the rows the MySQL provider reads from `column_metadata`. Such a table exposes real
typed columns. A table without one exposes the single `__json_string` column, read through the
`CLP_GET_*` UDFs. Both access paths are covered.

The list form matters: CLP stores a field under every type it was written with, and `ClpSchemaTree`
splits those into suffixed columns (`timestamp_bigint`, `timestamp_double`). A map keyed by field
name could only declare one, silently misrepresenting any polymorphic field.

## Known failures

Two tests are marked `xfail` and do not block.

The `timestamps` table stores one instant four ways: an ISO string, a float, and two integers
(microseconds and nanoseconds). All four read back as the same instant, but a filter on that column
matches only two of them -- the integer-encoded records are dropped.

```sql
SELECT timestamp_timestamp FROM timestamps;
-- 2025-04-30 08:50:05.000, four times

SELECT COUNT(*) FROM timestamps WHERE timestamp_timestamp = TIMESTAMP '2025-04-30 08:50:05';
-- 2
```

The two tests assert 4, so they fail today. `test_projected_and_filtered_disagree` asserts today's
numbers instead, so the suite fails if the behaviour ever changes. Fixing it needs a decision about
whether this connector or CLP should change.
