"""Fixtures shared by every test: the cluster itself, and a client that talks to it."""

from __future__ import annotations

import os
from pathlib import Path

import pytest

from clp_presto_integration_tests.client import PrestoClient
from clp_presto_integration_tests.cluster import wait_until_ready

# The defaults suit a run from the host, against the published port. The `tests` service overrides
# both, because it reaches the coordinator by service name over the compose network.
_COORDINATOR_HOST = os.environ.get("CLP_INTEGRATION_TEST_COORDINATOR_HOST", "localhost")
_COORDINATOR_PORT = int(os.environ.get("CLP_INTEGRATION_TEST_COORDINATOR_PORT", "18080"))

# The directory that docker-compose.yaml mounts into the cluster. The directory path is
# resolved in the same way as the compose file, so that both agree on which fixtures the
# cluster is serving.
_FIXTURE_DIR = Path(
    os.environ.get(
        "CLP_INTEGRATION_TEST_FIXTURE_DIR",
        Path(__file__).resolve().parents[1] / "fixtures",
    )
)


@pytest.fixture(scope="session")
def client() -> PrestoClient:
    """
    Returns a client connected to the cluster, for the whole session.

    The cluster must already be running; `task integration-tests:run` starts it beforehand and
    stops it afterwards.
    """
    presto = PrestoClient(_COORDINATOR_HOST, _COORDINATOR_PORT)
    wait_until_ready(presto)
    return presto


@pytest.fixture(scope="session")
def fixture_dir() -> Path:
    """The directory of fixtures that the cluster serves, one subdirectory per table."""
    return _FIXTURE_DIR


@pytest.fixture(scope="session")
def fixture_tables() -> list[str]:
    """The table names that the connector should report: one for each directory of fixtures."""
    tables = sorted(entry.name for entry in _FIXTURE_DIR.iterdir() if entry.is_dir())
    if not tables:
        msg = f"no fixture directories under {_FIXTURE_DIR}"
        raise AssertionError(msg)
    return tables
