"""Float precision: dictionary-encoded, formatted, and extreme values."""

from __future__ import annotations

from typing import TYPE_CHECKING

import pytest

if TYPE_CHECKING:
    from clp_presto_integration_tests.client import PrestoClient

pytestmark = pytest.mark.archive


def test_column_is_fully_populated(client: PrestoClient) -> None:
    """test_5.ndjson holds 36 records; its 37th line is blank."""
    rows = client.run("SELECT COUNT(*), COUNT(floatValue) FROM clp.default.float_precision")
    assert rows == [[36, 36]]


@pytest.mark.pushdown
def test_dictionary_encoded_float(client: PrestoClient) -> None:
    """Two records carry floatValue exactly 2, stored dictionary-encoded rather than formatted."""
    rows = client.run(
        "SELECT floatValue FROM clp.default.float_precision"
        " WHERE floatValue > 1.999999 AND floatValue < 2.000001"
    )
    assert rows == [[2.0], [2.0]]


@pytest.mark.pushdown
def test_formatted_float_precision_survives(client: PrestoClient) -> None:
    """A 1e-29 value round-trips exactly, so equality against the literal still matches."""
    matched = client.scalar(
        "SELECT COUNT(*) FROM clp.default.float_precision WHERE floatValue = 1.2345678912345E-29"
    )
    assert matched == 1


def test_extremes_survive_the_round_trip(client: PrestoClient) -> None:
    """The fixture spans -1e16 to DBL_MAX, so both ends exercise the encoding limits."""
    rows = client.run("SELECT MIN(floatValue), MAX(floatValue) FROM clp.default.float_precision")
    assert rows == [[-1e16, 1.7976931348623157e308]]
