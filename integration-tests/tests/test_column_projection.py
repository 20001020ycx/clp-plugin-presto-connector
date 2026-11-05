"""Field projection: typed columns, the raw record, and multi-archive tables."""

from __future__ import annotations

from typing import TYPE_CHECKING

import pytest

if TYPE_CHECKING:
    from clp_presto_integration_tests.client import PrestoClient

RAW_REQ_100 = (
    '{"timestamp":"2025-04-30T08:45:00Z","requestId":"req-100","userId":"user201",'
    '"method":"GET","path":"/api/users/1","responseTimeMs":25,"status":200}'
)


@pytest.mark.archive
def test_typed_projection(client: PrestoClient) -> None:
    """req-102's record carries no userId, so the column projects as NULL rather than empty."""
    rows = client.run(
        "SELECT requestId, userId, method FROM clp.default.http_logs"
        " WHERE method = 'GET' ORDER BY requestId"
    )
    assert rows == [
        ["req-100", "user201", "GET"],
        ["req-102", None, "GET"],
        ["req-105", "user204", "GET"],
        ["req-107", "user202", "GET"],
        ["req-109", "user203", "GET"],
    ]


@pytest.mark.udf
def test_raw_record_is_readable(client: PrestoClient) -> None:
    """The undecoded record is available as a column, byte-for-byte as it was compressed."""
    rows = client.run(
        "SELECT __json_string FROM clp.default.multi_archive"
        " WHERE CLP_GET_STRING('requestId') = 'req-100'"
    )
    assert rows == [[RAW_REQ_100]]


@pytest.mark.archive
def test_multi_archive_table_fans_out(client: PrestoClient) -> None:
    """A table of several archives yields one split each, so its rows are the sum of its parts."""
    total = client.scalar("SELECT COUNT(*) FROM clp.default.multi_archive")
    parts = sum(
        client.scalar(f"SELECT COUNT(*) FROM clp.default.{table}")
        for table in ("http_logs", "nested_events", "timestamps")
    )
    assert total == parts


@pytest.mark.archive
def test_legacy_format_archive_reads_identically(client: PrestoClient) -> None:
    """test_5.clps and test_5.v0.5.0.clps hold the same 36 records in different formats."""
    assert client.scalar("SELECT COUNT(*) FROM clp.default.legacy_archive") == client.scalar(
        "SELECT COUNT(*) FROM clp.default.float_precision"
    )
