"""Predicate pushdown: scalar, nested, and via the CLP_GET_* UDFs."""

from __future__ import annotations

from typing import TYPE_CHECKING

import pytest

if TYPE_CHECKING:
    from clp_presto_integration_tests.client import PrestoClient

pytestmark = pytest.mark.pushdown

# Matches exactly one record, whose userId is absent.
_POST_200 = "WHERE method = 'POST' AND status = 200"

# Of the WARNING/ERROR records, only the storage disk_usage one also satisfies the type/subtype
# clause.
_NESTED = (
    "WHERE event.severity IN ('WARNING','ERROR')"
    " AND ((event.type = 'network' AND event.subtype = 'connection')"
    " OR (event.type = 'storage' AND event.subtype LIKE 'disk%'))"
)


@pytest.mark.parametrize(
    "table",
    [
        pytest.param("http_logs", marks=pytest.mark.archive, id="archive"),
        pytest.param("http_logs_ir", marks=pytest.mark.ir, id="ir"),
    ],
)
def test_scalar_predicates(client: PrestoClient, table: str) -> None:
    """A conjunction over two scalar columns reaches the archives on both split types."""
    rows = client.run(f"SELECT requestId, userId, path FROM clp.default.{table} {_POST_200}")
    assert rows == [["req-106", None, "/auth/login"]]


@pytest.mark.parametrize(
    "table",
    [
        pytest.param("nested_events", marks=pytest.mark.archive, id="archive"),
        pytest.param("nested_events_ir", marks=pytest.mark.ir, id="ir"),
    ],
)
def test_nested_predicates(client: PrestoClient, table: str) -> None:
    """Predicates over nested object fields push down the same way that scalar ones do."""
    rows = client.run(f"SELECT event.type, event.subtype FROM clp.default.{table} {_NESTED}")
    assert rows == [["storage", "disk_usage"]]


@pytest.mark.udf
def test_predicates_through_udfs(client: PrestoClient) -> None:
    """Runs the same predicate through the CLP_GET_* UDFs, so that both access paths are covered."""
    rows = client.run(
        "SELECT CLP_GET_STRING('requestId'), CLP_GET_STRING('path')"
        " FROM clp.default.multi_archive"
        " WHERE CLP_GET_STRING('method') = 'POST' AND CLP_GET_BIGINT('status') = 200"
    )
    assert rows == [["req-106", "/auth/login"]]


def test_array_field(client: PrestoClient) -> None:
    """
    Array elements come back JSON-quoted, and this test asserts that as it stands.

    Whether `"filesystem"` should really carry embedded quotes is worth a separate look.
    """
    rows = client.run(
        "SELECT event.tags FROM clp.default.nested_events WHERE event.type = 'storage'"
    )
    assert rows == [[['"filesystem"', '"monitoring"']]]
