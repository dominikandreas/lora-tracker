from __future__ import annotations

from dataclasses import dataclass
import json
from pathlib import Path
import sqlite3
import threading
from typing import Any


@dataclass(frozen=True)
class QueryResult:
    points: list[dict[str, Any]]
    has_more: bool
    next_cursor: int


class HistoryStore:
    def __init__(self, path: Path | str):
        self.path = Path(path)
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self._lock = threading.RLock()
        self._db = sqlite3.connect(self.path, check_same_thread=False)
        self._db.row_factory = sqlite3.Row
        with self._db:
            self._db.execute("PRAGMA journal_mode=WAL")
            self._db.execute("PRAGMA synchronous=NORMAL")
            self._db.execute(
                """
                CREATE TABLE IF NOT EXISTS points (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    point_id TEXT NOT NULL UNIQUE,
                    device_hash TEXT NOT NULL,
                    device_id TEXT NOT NULL,
                    device_name TEXT NOT NULL,
                    gateway_hash TEXT NOT NULL,
                    gateway_id TEXT NOT NULL,
                    received_at_ms INTEGER NOT NULL,
                    fix_time_unix_ms INTEGER NOT NULL DEFAULT 0,
                    timestamp_valid INTEGER NOT NULL DEFAULT 0,
                    time_source TEXT NOT NULL DEFAULT 'unavailable',
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
            self._migrate_points_table()
            self._db.execute(
                "CREATE INDEX IF NOT EXISTS idx_points_tracker_time "
                "ON points(device_hash, fix_time_unix_ms, received_at_ms, id)"
            )
            self._db.execute(
                """
                CREATE TABLE IF NOT EXISTS receptions (
                    point_id TEXT NOT NULL,
                    gateway_hash TEXT NOT NULL,
                    gateway_id TEXT NOT NULL,
                    first_received_at_ms INTEGER NOT NULL,
                    last_received_at_ms INTEGER NOT NULL,
                    rssi INTEGER NOT NULL,
                    reception_count INTEGER NOT NULL DEFAULT 1,
                    PRIMARY KEY(point_id, gateway_hash),
                    FOREIGN KEY(point_id) REFERENCES points(point_id) ON DELETE CASCADE
                )
                """
            )

    def _migrate_points_table(self) -> None:
        columns = {
            row["name"]
            for row in self._db.execute("PRAGMA table_info(points)").fetchall()
        }
        migrations = {
            "fix_time_unix_ms": (
                "ALTER TABLE points ADD COLUMN "
                "fix_time_unix_ms INTEGER NOT NULL DEFAULT 0"
            ),
            "timestamp_valid": (
                "ALTER TABLE points ADD COLUMN "
                "timestamp_valid INTEGER NOT NULL DEFAULT 0"
            ),
            "time_source": (
                "ALTER TABLE points ADD COLUMN "
                "time_source TEXT NOT NULL DEFAULT 'unavailable'"
            ),
        }
        for column, statement in migrations.items():
            if column not in columns:
                self._db.execute(statement)

    def close(self) -> None:
        with self._lock:
            self._db.close()

    def insert_point(self, point: dict[str, Any], received_at_ms: int) -> bool:
        payload_json = json.dumps(point, separators=(",", ":"), sort_keys=True)
        timestamp_valid = bool(point.get("timestamp_valid", False))
        fix_time_unix_ms = int(point.get("fix_time_unix_ms", 0))
        time_source = str(point.get("time_source", "unavailable"))
        values = (
            point["point_id"],
            point["device_hash"],
            point.get("device_id", ""),
            point.get("device_name", ""),
            point.get("gateway_hash", ""),
            point.get("gateway_id", ""),
            received_at_ms,
            fix_time_unix_ms,
            1 if timestamp_valid else 0,
            time_source,
            point["latitude"],
            point["longitude"],
            point["dist_m"],
            point["battery_level"],
            point["rssi"],
            point["boot_id"],
            point["seq"],
            payload_json,
        )
        with self._lock, self._db:
            cursor = self._db.execute(
                """
                INSERT OR IGNORE INTO points (
                    point_id, device_hash, device_id, device_name,
                    gateway_hash, gateway_id, received_at_ms,
                    fix_time_unix_ms, timestamp_valid, time_source,
                    latitude, longitude, dist_m, battery_level, rssi,
                    boot_id, seq, payload_json
                ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                """,
                values,
            )
            inserted = cursor.rowcount == 1
            self._db.execute(
                """
                INSERT INTO receptions (
                    point_id, gateway_hash, gateway_id, first_received_at_ms,
                    last_received_at_ms, rssi, reception_count
                ) VALUES (?, ?, ?, ?, ?, ?, 1)
                ON CONFLICT(point_id, gateway_hash) DO UPDATE SET
                    last_received_at_ms = excluded.last_received_at_ms,
                    rssi = MAX(receptions.rssi, excluded.rssi),
                    reception_count = receptions.reception_count + 1
                """,
                (
                    point["point_id"],
                    point.get("gateway_hash", ""),
                    point.get("gateway_id", ""),
                    received_at_ms,
                    received_at_ms,
                    point["rssi"],
                ),
            )
            return inserted

    def query(
        self,
        *,
        device_hash: str,
        from_unix_ms: int,
        to_unix_ms: int,
        limit: int,
        cursor: int = 0,
    ) -> QueryResult:
        # GNSS fix time is authoritative when available. Legacy points fall back
        # to server receive time, preserving useful history during migration.
        effective_time = (
            "CASE WHEN p.timestamp_valid = 1 AND p.fix_time_unix_ms > 0 "
            "THEN p.fix_time_unix_ms ELSE p.received_at_ms END"
        )
        with self._lock:
            rows = self._db.execute(
                f"""
                SELECT p.id, p.received_at_ms, p.fix_time_unix_ms,
                       p.timestamp_valid, p.time_source, p.payload_json,
                       {effective_time} AS effective_time_unix_ms,
                       COUNT(r.gateway_hash) AS reception_gateway_count,
                       MAX(r.rssi) AS best_rssi
                FROM points AS p
                LEFT JOIN receptions AS r ON r.point_id = p.point_id
                WHERE p.device_hash = ?
                  AND {effective_time} BETWEEN ? AND ?
                  AND p.id > ?
                GROUP BY p.id
                ORDER BY p.id ASC
                LIMIT ?
                """,
                (device_hash, from_unix_ms, to_unix_ms, cursor, limit + 1),
            ).fetchall()

        has_more = len(rows) > limit
        rows = rows[:limit]
        points: list[dict[str, Any]] = []
        next_cursor = cursor
        for row in rows:
            point = json.loads(row["payload_json"])
            point["received_at_ms"] = row["received_at_ms"]
            point["fix_time_unix_ms"] = row["fix_time_unix_ms"]
            point["timestamp_valid"] = bool(row["timestamp_valid"])
            point["time_source"] = row["time_source"]
            point["effective_time_unix_ms"] = row["effective_time_unix_ms"]
            point["reception_gateway_count"] = row[
                "reception_gateway_count"
            ]
            point["best_rssi"] = row["best_rssi"]
            points.append(point)
            next_cursor = row["id"]
        return QueryResult(points, has_more, next_cursor)

    def purge_before(self, cutoff_unix_ms: int) -> int:
        # Retention is based on effective observation time, not delayed MQTT
        # delivery time. Legacy points use receive time.
        with self._lock, self._db:
            point_ids = [
                row[0]
                for row in self._db.execute(
                    """
                    SELECT point_id FROM points
                    WHERE (CASE
                      WHEN timestamp_valid = 1 AND fix_time_unix_ms > 0
                      THEN fix_time_unix_ms ELSE received_at_ms END) < ?
                    """,
                    (cutoff_unix_ms,),
                ).fetchall()
            ]
            if point_ids:
                self._db.executemany(
                    "DELETE FROM receptions WHERE point_id = ?",
                    ((point_id,) for point_id in point_ids),
                )
            cursor = self._db.execute(
                """
                DELETE FROM points
                WHERE (CASE
                  WHEN timestamp_valid = 1 AND fix_time_unix_ms > 0
                  THEN fix_time_unix_ms ELSE received_at_ms END) < ?
                """,
                (cutoff_unix_ms,),
            )
            return cursor.rowcount

    def count(self) -> int:
        with self._lock:
            row = self._db.execute(
                "SELECT COUNT(*) AS count FROM points"
            ).fetchone()
            return int(row["count"])
