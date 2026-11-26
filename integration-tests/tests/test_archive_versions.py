"""Archive format versions: the current one reads, and a newer one fails with a clear error."""

from __future__ import annotations

import re
from typing import TYPE_CHECKING

import prestodb.exceptions
import pytest

if TYPE_CHECKING:
    from clp_presto_integration_tests.client import PrestoClient

pytestmark = pytest.mark.archive


def test_current_format_archive_reads_identically(client: PrestoClient) -> None:
    """test_5.clps and test_5.v0.7.0.clps hold the same 36 records in different formats."""
    assert client.scalar("SELECT COUNT(*) FROM clp.default.archive_v0_7_0") == client.scalar(
        "SELECT COUNT(*) FROM clp.default.float_precision"
    )


@pytest.mark.parametrize("catalog", ["clp", "clp_s3"])
@pytest.mark.parametrize(
    "table",
    [
        # The v0.7.0 archive with its header's version raised to 0.8.0, so it still opens.
        "future_version_archive",
        # Only that archive's header, so opening it fails and the header is read on its own.
        "future_version_unreadable_archive",
    ],
)
def test_newer_format_archive_fails_with_version_error(
    client: PrestoClient, catalog: str, table: str
) -> None:
    """A newer archive fails the query and names both versions, rather than returning no rows."""
    expected = re.escape(
        "uses clp-s archive format version 0.8.0, but this connector only reads versions up to"
        " 0.7.0"
    )
    with pytest.raises(prestodb.exceptions.PrestoQueryError, match=expected):
        client.run(f"SELECT COUNT(*) FROM {catalog}.default.{table}")
