"""Timestamp marshalling across storage encodings, on the archive and IR paths."""

from __future__ import annotations

from typing import TYPE_CHECKING

import pytest

if TYPE_CHECKING:
    from clp_presto_integration_tests.client import PrestoClient

# How Presto renders the instant that test_3 encodes four ways, and how it is written as a SQL
# literal. Presto renders timestamps with milliseconds; the literal carries none.
_INSTANT = "2025-04-30 08:50:05.000"
_INSTANT_LITERAL = "2025-04-30 08:50:05"


@pytest.mark.archive
def test_every_encoding_marshals_to_one_instant(client: PrestoClient) -> None:
    """
    One instant, four encodings.

    test_3 encodes one instant four ways -- ISO string, float seconds, microseconds and
    nanoseconds -- and all four must read back as the same timestamp.
    """
    rows = client.run("SELECT timestamp_timestamp FROM clp.default.timestamps")
    assert [row[0] for row in rows] == [_INSTANT] * 4


@pytest.mark.ir
def test_ir_timestamps(client: PrestoClient) -> None:
    """The IR path marshals the same encodings, including a record with no timestamp at all."""
    rows = client.run("SELECT timestamp_timestamp FROM clp.default.timestamps_ir ORDER BY 1")
    assert rows == [[_INSTANT], ["2025-12-17 20:23:25.000"], [None]]


@pytest.mark.xfail(
    reason=(
        "Non-blocking: filtering on a timestamp column drops the integer-encoded records, even"
        " though all four read back as the same instant. Fixing it needs its own investigation;"
        " see test_projected_and_filtered_disagree below."
    ),
    strict=False,
)
@pytest.mark.pushdown
def test_equality_matches_every_encoding(client: PrestoClient) -> None:
    """Every encoding denotes the same instant, so equality should match all four records."""
    matched = client.scalar(
        "SELECT COUNT(*) FROM clp.default.timestamps"
        f" WHERE timestamp_timestamp = TIMESTAMP '{_INSTANT_LITERAL}'"
    )
    assert matched == 4


@pytest.mark.xfail(
    reason=(
        "Non-blocking: the same problem on the IR path, where the filter drops the only"
        " matching record."
    ),
    strict=False,
)
@pytest.mark.pushdown
@pytest.mark.ir
def test_ir_range_pushdown(client: PrestoClient) -> None:
    """The one record before the threshold should survive the filter."""
    matched = client.scalar(
        "SELECT COUNT(*) FROM clp.default.timestamps_ir"
        " WHERE timestamp_timestamp < TIMESTAMP '2025-06-01 00:00:00'"
    )
    assert matched == 1


@pytest.mark.pushdown
def test_projected_and_filtered_disagree(client: PrestoClient) -> None:
    """
    Pins the shape of the defect above, so that a fix has an unambiguous target.

    The same comparison is true for every record when projected, yet filtering on it drops two.
    This test asserts the discrepancy so that it cannot regress further unnoticed. When the
    underlying bug is fixed, the two xfails above flip to XPASS and this one should be deleted.
    """
    projected = client.run(
        f"SELECT timestamp_timestamp = TIMESTAMP '{_INSTANT_LITERAL}' FROM clp.default.timestamps"
    )
    filtered = client.scalar(
        "SELECT COUNT(*) FROM clp.default.timestamps"
        f" WHERE timestamp_timestamp = TIMESTAMP '{_INSTANT_LITERAL}'"
    )
    assert [row[0] for row in projected] == [True] * 4
    assert filtered == 2
