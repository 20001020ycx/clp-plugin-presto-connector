"""
Runtime metrics: the connector's time breakdown, read back from the query info.

`docs/runtime-metrics.md` defines every metric checked here.
"""

from __future__ import annotations

from typing import Any, TYPE_CHECKING

import pytest

if TYPE_CHECKING:
    from pathlib import Path

    from clp_presto_integration_tests.client import PrestoClient

pytestmark = pytest.mark.archive

_TABLE = "multi_archive"

# The phases that `loadSplit` runs one after another; together they lie inside it.
_LOAD_SPLIT_STEPS = (
    "clpOpenArchive",
    "clpTimestampIndexAndSchemaMatch",
    "clpProjection",
    "clpReadTableMetadata",
    "clpLoadDictionaries",
    "clpOpenPackedStreams",
    "clpQueryRunnerInit",
)

# Metrics that the coordinator records while planning and scheduling any scan of the table.
_COORDINATOR_METRICS = (
    "clpMetadataGetTableHandleNanos",
    "clpUdfRewriterNanos",
    "clpComputePushDownNanos",
    "clpGetSplitsNanos",
    "clpListSplitsNanos",
    "clpSplitCount",
)


def _table_scans(query_info: dict[str, Any]) -> list[dict[str, Any]]:
    summaries = query_info["queryStats"]["operatorSummaries"]
    scans = [summary for summary in summaries if "TableScan" in summary["operatorType"]]
    assert scans, f"no table scan among {[s['operatorType'] for s in summaries]}"
    return scans


def _worker_metric(scans: list[dict[str, Any]], name: str) -> dict[str, int]:
    """Merges `name` across the scan's operator summaries, as `{"sum": ..., "count": ...}`."""
    merged = {"sum": 0, "count": 0}
    found = False
    for scan in scans:
        for key, metric in scan.get("runtimeStats", {}).items():
            if key == name or key.endswith(f".{name}"):
                merged["sum"] += int(metric["sum"])
                merged["count"] += int(metric["count"])
                found = True
    assert found, f"{name} missing from the table scan's runtime stats"
    return merged


def _sum(scans: list[dict[str, Any]], name: str) -> int:
    return _worker_metric(scans, name)["sum"]


@pytest.fixture(scope="module", params=["clp", "clp_s3"])
def scan(request: pytest.FixtureRequest, client: PrestoClient) -> dict[str, Any]:
    """Reads the whole multi-archive table once per storage backend, keeping the query info."""
    rows, query_info = client.run_with_query_info(f"SELECT * FROM {request.param}.default.{_TABLE}")
    assert len(rows) == 24
    return query_info


def test_coordinator_metrics(scan: dict[str, Any]) -> None:
    """Planning and split listing are timed, and the split count is the table's archive count."""
    runtime_stats = scan["queryStats"]["runtimeStats"]
    for name in _COORDINATOR_METRICS:
        assert name in runtime_stats, f"{name} missing from {sorted(runtime_stats)}"
    assert int(runtime_stats["clpSplitCount"]["sum"]) == 3
    assert int(runtime_stats["clpListSplitsNanos"]["sum"]) <= int(
        runtime_stats["clpGetSplitsNanos"]["sum"]
    )


def test_every_split_is_counted_once(scan: dict[str, Any]) -> None:
    """The workers report each split the coordinator listed: added, read and closed once."""
    scans = _table_scans(scan)
    splits = int(scan["queryStats"]["runtimeStats"]["clpSplitCount"]["sum"])
    assert _sum(scans, "clpSplits") == splits
    for phase in ("clpAddSplit", "clpNext", "clpCloseArchive", "clpLoadSplit"):
        assert _worker_metric(scans, f"{phase}WallNanos")["count"] == splits, phase
        assert _worker_metric(scans, f"{phase}CpuNanos")["count"] == splits, phase


def test_sub_phases_lie_inside_their_parents(scan: dict[str, Any]) -> None:
    """A phase's time never exceeds that of the phase it runs inside."""
    scans = _table_scans(scan)
    assert _sum(scans, "clpParseQueryWallNanos") <= _sum(scans, "clpAddSplitWallNanos")
    load_split = _sum(scans, "clpLoadSplitWallNanos")
    assert load_split <= _sum(scans, "clpNextWallNanos")
    assert sum(_sum(scans, f"{step}WallNanos") for step in _LOAD_SPLIT_STEPS) <= load_split
    assert _sum(scans, "clpReadTableWallNanos") <= _sum(scans, "clpNextWallNanos")
    assert _sum(scans, "clpStorageWaitNanos") <= _sum(scans, "clpNextWallNanos") + _sum(
        scans, "clpCloseArchiveWallNanos"
    )


def test_rows_are_counted(scan: dict[str, Any]) -> None:
    """Without a filter, every row scanned matches and reaches the output."""
    scans = _table_scans(scan)
    assert _sum(scans, "clpRowsScanned") == 24
    assert _sum(scans, "clpRowsMatched") == 24


def test_storage_bytes_match_the_archives(scan: dict[str, Any], fixture_dir: Path) -> None:
    """
    Every archive is opened and closed through the counting reader, and reading the whole table
    consumes each archive exactly once. Velox's own raw input size reports the same bytes.
    """
    scans = _table_scans(scan)
    splits = int(scan["queryStats"]["runtimeStats"]["clpSplitCount"]["sum"])
    assert _sum(scans, "clpStorageReadersOpened") == splits
    assert _sum(scans, "clpStorageReadersClosed") == splits

    archive_bytes = sum(path.stat().st_size for path in (fixture_dir / _TABLE).glob("*.clps"))
    consumed = _sum(scans, "clpStorageBytesConsumed")
    assert consumed == archive_bytes
    assert sum(int(s["rawInputDataSizeInBytes"]) for s in scans) == consumed
