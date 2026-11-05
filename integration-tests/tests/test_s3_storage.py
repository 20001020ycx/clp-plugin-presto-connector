"""S3-backed reads: the `clp_s3` catalog serves the same archives out of MinIO."""

from __future__ import annotations

from typing import TYPE_CHECKING

import pytest

if TYPE_CHECKING:
    from clp_presto_integration_tests.client import PrestoClient

pytestmark = pytest.mark.archive


def test_matches_the_filesystem_catalog(client: PrestoClient) -> None:
    """`clp_s3` and `clp` differ only in storage backend, so they return the same rows."""

    # Sorted as text: workers emit rows in whatever order they finish, and a row may hold nulls
    # that make the values themselves unorderable.
    def rows(catalog: str) -> list[str]:
        return sorted(str(row) for row in client.run(f"SELECT * FROM {catalog}.default.http_logs"))

    assert rows("clp_s3") == rows("clp")


def test_reads_every_archive_of_a_table(client: PrestoClient) -> None:
    """A table split across archives needs every object fetched, not just the first."""
    assert client.scalar("SELECT COUNT(*) FROM clp_s3.default.multi_archive") == 24


@pytest.mark.pushdown
def test_pushed_down_filter(client: PrestoClient) -> None:
    """A filter that reaches the worker proves that the object is decoded, not merely listed."""
    assert client.scalar('SELECT COUNT(*) FROM clp_s3.default.http_logs WHERE "status" = 200') == 8
