from pathlib import Path
import sqlite3

from equine_archiver.store import HistoryStore

HASH = "3db3edf61a18fac0"


def make_point(seq: int, *, fix_time_ms: int | None = None):
    valid = fix_time_ms is not None
    return {
        "api_version": 1,
        "point_schema_version": 2,
        "device_hash": HASH,
        "point_id": f"{HASH}:7:{seq}",
        "device_id": "wera",
        "device_name": "Wera",
        "gateway_hash": "1111111111111111",
        "gateway_id": "home",
        "latitude": 50.0 + seq / 1000,
        "longitude": 8.0,
        "dist_m": seq * 10,
        "battery_level": 80,
        "rssi": -90,
        "boot_id": 7,
        "seq": seq,
        "timestamp_valid": valid,
        "fix_time_unix_ms": fix_time_ms or 0,
        "time_source": "gnss" if valid else "unavailable",
    }


def test_insert_deduplicate_query_and_purge(tmp_path: Path):
    store = HistoryStore(tmp_path / "history.sqlite3")
    try:
        assert store.insert_point(make_point(1, fix_time_ms=10_000), 100_000) is True
        assert store.insert_point(make_point(1, fix_time_ms=10_000), 100_001) is False
        assert store.insert_point(make_point(2, fix_time_ms=20_000), 200_000) is True
        assert store.insert_point(make_point(3, fix_time_ms=30_000), 300_000) is True
        result = store.query(
            device_hash=HASH,
            from_unix_ms=0,
            to_unix_ms=25_000,
            limit=2,
            cursor=0,
        )
        assert [p["seq"] for p in result.points] == [1, 2]
        assert result.points[0]["effective_time_unix_ms"] == 10_000
        assert result.has_more is False
        assert store.purge_before(25_000) == 2
        assert store.count() == 1
    finally:
        store.close()


def test_receive_time_fallback_for_legacy_timestamp(tmp_path: Path):
    store = HistoryStore(tmp_path / "history.sqlite3")
    try:
        assert store.insert_point(make_point(1), 50_000)
        result = store.query(
            device_hash=HASH,
            from_unix_ms=40_000,
            to_unix_ms=60_000,
            limit=10,
        )
        assert result.points[0]["effective_time_unix_ms"] == 50_000
        assert result.points[0]["timestamp_valid"] is False
    finally:
        store.close()


def test_existing_v1_database_is_migrated(tmp_path: Path):
    path = tmp_path / "history.sqlite3"
    db = sqlite3.connect(path)
    db.execute(
        """
        CREATE TABLE points (
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          point_id TEXT NOT NULL UNIQUE,
          device_hash TEXT NOT NULL,
          device_id TEXT NOT NULL,
          device_name TEXT NOT NULL,
          gateway_hash TEXT NOT NULL,
          gateway_id TEXT NOT NULL,
          received_at_ms INTEGER NOT NULL,
          latitude REAL NOT NULL,
          longitude REAL NOT NULL,
          dist_m INTEGER NOT NULL,
          battery_level INTEGER NOT NULL,
          rssi INTEGER NOT NULL,
          boot_id INTEGER NOT NULL,
          seq INTEGER NOT NULL,
          payload_json TEXT NOT NULL
        )
        """
    )
    db.commit()
    db.close()
    store = HistoryStore(path)
    try:
        columns = {
            row[1]
            for row in store._db.execute("PRAGMA table_info(points)").fetchall()
        }
        assert {"fix_time_unix_ms", "timestamp_valid", "time_source"} <= columns
    finally:
        store.close()


def test_multiple_gateway_receptions_are_preserved(tmp_path: Path):
    store = HistoryStore(tmp_path / "history.sqlite3")
    try:
        first = make_point(1, fix_time_ms=10_000)
        second = dict(first)
        second["gateway_hash"] = "2222222222222222"
        second["gateway_id"] = "field"
        second["rssi"] = -72
        assert store.insert_point(first, 1000) is True
        assert store.insert_point(second, 1100) is False
        result = store.query(
            device_hash=HASH,
            from_unix_ms=0,
            to_unix_ms=20_000,
            limit=10,
            cursor=0,
        )
        assert result.points[0]["reception_gateway_count"] == 2
        assert result.points[0]["best_rssi"] == -72
    finally:
        store.close()
