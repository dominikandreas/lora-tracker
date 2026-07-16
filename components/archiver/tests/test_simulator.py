from pathlib import Path
import os
import shlex
import shutil

import pytest

from lora_tracker_archiver.simulator import (
    run_embedded_simulation,
    run_service_simulation,
    run_simulation,
)
from lora_tracker_archiver.store import HistoryStore


def _host_cpp_compiler_available() -> bool:
    configured = shlex.split(os.environ.get("CXX", ""))
    if configured:
        return shutil.which(configured[0]) is not None
    return any(shutil.which(candidate) for candidate in ("g++", "clang++", "c++"))


def test_end_to_end_simulation_exercises_deduplication_and_history(tmp_path: Path):
    path = tmp_path / "simulation.sqlite3"
    summary = run_simulation(
        database=path,
        tracker_count=2,
        points_per_tracker=6,
    )

    assert summary.tracker_points_emitted == 12
    assert summary.gateway_events_published == 24
    assert summary.points_archived == 12
    assert summary.duplicate_receptions == 12
    assert summary.history_points_returned == 12
    assert summary.history_responses == 4  # two 5+1 point responses per tracker

    store = HistoryStore(path)
    try:
        first = store.query(
            device_hash="3db3edf61a18fac0",
            from_unix_ms=0,
            to_unix_ms=2_000_000_000_000,
            limit=10,
        ).points
        assert first[0]["reception_gateway_count"] == 2
        assert first[4]["timestamp_valid"] is False
        assert first[4]["effective_time_unix_ms"] == first[4]["received_at_ms"]
    finally:
        store.close()


def test_archiver_service_simulation_covers_mqtt_lifecycle_and_errors(tmp_path: Path):
    result = run_service_simulation(database=tmp_path / "service.sqlite3")
    assert result == {
        "subscriptions": 2,
        "stored_points": 4,
        "points_received": 5,
        "points_inserted": 4,
        "history_chunks": 2,
        "history_error_responses": 1,
    }


@pytest.mark.skipif(
    not _host_cpp_compiler_available(),
    reason="embedded simulator requires a host C++ compiler",
)
def test_embedded_simulation_compiles_and_runs_all_firmware_contracts():
    result = run_embedded_simulation()
    assert result["components"] == [
        "tracker-firmware",
        "gateway-firmware",
        "repeater-firmware",
    ]
