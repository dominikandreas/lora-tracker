import json
from pathlib import Path
import sqlite3
import subprocess
import sys

import pytest

from lora_tracker_archiver.maintenance import (
    DatabaseProcessLock,
    backup_database,
    check_database,
    restore_database,
    retention_cutoff_ms,
)
from lora_tracker_archiver.store import HistoryStore


HASH = "3db3edf61a18fac0"


def point(sequence: int) -> dict[str, object]:
    return {
        "point_id": f"{HASH}:1:{sequence}",
        "device_hash": HASH,
        "device_id": "tracker",
        "device_name": "Tracker",
        "gateway_hash": "1111111111111111",
        "gateway_id": "gateway",
        "latitude": 50.0,
        "longitude": 8.0,
        "dist_m": sequence,
        "battery_level": 80,
        "rssi": -90,
        "boot_id": 1,
        "seq": sequence,
        "timestamp_valid": False,
        "fix_time_unix_ms": 0,
        "time_source": "unavailable",
    }


def create_archive(path: Path, sequences: tuple[int, ...]) -> None:
    store = HistoryStore(path)
    try:
        for sequence in sequences:
            assert store.insert_point(point(sequence), sequence * 1000)
    finally:
        store.close()


def test_live_backup_is_consistent_and_does_not_require_wal_copy(tmp_path: Path):
    live = tmp_path / "live.sqlite3"
    backup = tmp_path / "backup.sqlite3"
    store = HistoryStore(live)
    try:
        assert store.insert_point(point(1), 1000)
        report = backup_database(live, backup)
    finally:
        store.close()

    assert report.ok
    assert report.points == 1
    assert report.receptions == 1
    assert not Path(f"{backup}-wal").exists()
    with pytest.raises(FileExistsError):
        backup_database(live, backup)


def test_restore_requires_force_and_keeps_pre_restore_snapshot(tmp_path: Path):
    live = tmp_path / "live.sqlite3"
    source = tmp_path / "source.sqlite3"
    create_archive(live, (1,))
    create_archive(source, (2, 3))

    with pytest.raises(FileExistsError):
        restore_database(source, live)

    report, pre_restore = restore_database(source, live, force=True)
    assert report.ok and report.points == 2
    assert pre_restore is not None and pre_restore.is_file()
    assert check_database(pre_restore).points == 1


def test_restore_rejects_non_archive_database(tmp_path: Path):
    invalid = tmp_path / "invalid.sqlite3"
    with sqlite3.connect(invalid) as database:
        database.execute("CREATE TABLE unrelated(value TEXT)")

    report = check_database(invalid)
    assert not report.ok
    with pytest.raises(RuntimeError, match="invalid archive"):
        restore_database(invalid, tmp_path / "restored.sqlite3")


def test_restore_refuses_database_held_by_service_lock(tmp_path: Path):
    live = tmp_path / "live.sqlite3"
    source = tmp_path / "source.sqlite3"
    create_archive(live, (1,))
    create_archive(source, (2,))

    with DatabaseProcessLock(live):
        with pytest.raises(RuntimeError, match="archive is in use"):
            restore_database(source, live, force=True)


def test_retention_preview_count_matches_applied_purge(tmp_path: Path):
    live = tmp_path / "live.sqlite3"
    create_archive(live, (1, 2, 3))
    store = HistoryStore(live)
    try:
        assert store.count_before(2500) == 2
        assert store.count() == 3
        assert store.purge_before(2500) == 2
        store.checkpoint()
        assert store.count() == 1
    finally:
        store.close()

    assert retention_cutoff_ms(10, now_s=1_000_000) == 136_000_000
    with pytest.raises(ValueError):
        retention_cutoff_ms(0)


def test_cli_safety_failure_is_machine_readable(tmp_path: Path):
    missing = tmp_path / "missing.sqlite3"
    result = subprocess.run(
        [
            sys.executable,
            "-m",
            "lora_tracker_archiver",
            "check",
            "--database",
            str(missing),
        ],
        check=False,
        capture_output=True,
        text=True,
    )
    assert result.returncode == 2
    error = json.loads(result.stderr)
    assert error["ok"] is False
    assert error["operation"] == "check"
    assert "does not exist" in error["error"]
    assert "Traceback" not in result.stderr
