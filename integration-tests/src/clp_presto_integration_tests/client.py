"""Provides access to a coordinator through Presto's own Python client."""

from __future__ import annotations

import time
from typing import Any

import prestodb.dbapi
import requests

_CLUSTER_REQUEST_TIMEOUT_SECONDS = 30
_FINAL_QUERY_INFO_TIMEOUT_SECONDS = 30
_FINAL_QUERY_INFO_POLL_SECONDS = 0.5


class PrestoClient:
    """A client for one coordinator, scoped to the `clp` catalog's `default` schema."""

    def __init__(self, host: str, port: int, user: str = "integration-test") -> None:
        """Stores the connection details. Nothing connects until a statement runs."""
        self._host = host
        self._port = port
        self._user = user

    def run(self, sql: str) -> list[list[Any]]:
        """Runs `sql` and returns its rows, in whatever order the workers produce them."""
        with self._connect() as connection:
            cursor = connection.cursor()
            cursor.execute(sql)
            return [list(row) for row in cursor.fetchall()]

    def run_with_query_info(self, sql: str) -> tuple[list[list[Any]], dict[str, Any]]:
        """Runs `sql` and returns its rows along with the query's final query info."""
        with self._connect() as connection:
            cursor = connection.cursor()
            cursor.execute(sql)
            rows = [list(row) for row in cursor.fetchall()]
            query_id = cursor.stats["queryId"]
        return rows, self.query_info(query_id)

    def query_info(self, query_id: str) -> dict[str, Any]:
        """
        Returns the query info (`GET /v1/query/{queryId}`) of a finished query.

        The coordinator answers the client before every task has reported its final statistics,
        so this waits until the info is marked final.
        """
        deadline = time.monotonic() + _FINAL_QUERY_INFO_TIMEOUT_SECONDS
        while True:
            response = requests.get(
                f"http://{self._host}:{self._port}/v1/query/{query_id}",
                headers={"X-Presto-User": self._user},
                timeout=_CLUSTER_REQUEST_TIMEOUT_SECONDS,
            )
            response.raise_for_status()
            info: dict[str, Any] = response.json()
            if info.get("finalQueryInfo"):
                return info
            if time.monotonic() >= deadline:
                msg = f"query info of {query_id} not final in time"
                raise TimeoutError(msg)
            time.sleep(_FINAL_QUERY_INFO_POLL_SECONDS)

    def scalar(self, sql: str) -> Any:
        """Runs `sql`, which must return exactly one row holding exactly one column."""
        rows = self.run(sql)
        if 1 != len(rows) or 1 != len(rows[0]):
            msg = f"expected exactly one value, got {rows!r}"
            raise ValueError(msg)
        return rows[0][0]

    def is_ready(self) -> bool:
        """
        Reports whether the coordinator has a worker that can run queries.

        This checks `activeWorkers > 0` rather than checking for an HTTP 200, because a worker
        whose `presto.version` differs from the coordinator's still answers health checks. The
        coordinator never schedules a query onto such a worker, so queries queue forever.
        """
        try:
            response = requests.get(
                f"http://{self._host}:{self._port}/v1/cluster",
                timeout=_CLUSTER_REQUEST_TIMEOUT_SECONDS,
            )
            response.raise_for_status()
            return 0 < int(response.json().get("activeWorkers", 0))
        except (requests.RequestException, ValueError):
            return False

    def _connect(self) -> prestodb.dbapi.Connection:
        return prestodb.dbapi.Connection(
            host=self._host,
            port=self._port,
            user=self._user,
            catalog="clp",
            schema="default",
        )
