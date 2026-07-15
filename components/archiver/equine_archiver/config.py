from __future__ import annotations

from dataclasses import dataclass
import os
from pathlib import Path


def _env_bool(name: str, default: bool = False) -> bool:
    raw = os.getenv(name)
    if raw is None:
        return default
    return raw.strip().lower() in {"1", "true", "yes", "on"}


def _env_int(name: str, default: int, minimum: int, maximum: int) -> int:
    raw = os.getenv(name)
    value = default if raw is None else int(raw)
    if not minimum <= value <= maximum:
        raise ValueError(f"{name} must be between {minimum} and {maximum}")
    return value


@dataclass(frozen=True)
class ArchiverConfig:
    mqtt_host: str
    mqtt_port: int
    mqtt_username: str | None
    mqtt_password: str | None
    mqtt_tls: bool
    mqtt_ca_file: Path | None
    base_topic: str
    archiver_id: str
    database_path: Path
    retention_days: int
    tracker_hashes: frozenset[str]
    response_chunk_points: int
    maximum_request_points: int

    @classmethod
    def from_env(cls) -> "ArchiverConfig":
        hashes = frozenset(
            value.strip().lower()
            for value in os.getenv("TRACKER_HASHES", "").split(",")
            if value.strip()
        )
        for device_hash in hashes:
            if len(device_hash) != 16 or any(c not in "0123456789abcdef" for c in device_hash):
                raise ValueError(f"Invalid TRACKER_HASHES entry: {device_hash}")

        archiver_id = os.getenv("ARCHIVER_ID", "default").strip()
        if not archiver_id or any(c not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_" for c in archiver_id):
            raise ValueError("ARCHIVER_ID may contain only letters, digits, '-' and '_'")

        base_topic = os.getenv("MQTT_BASE_TOPIC", "equine").strip().strip("/")
        if not base_topic:
            raise ValueError("MQTT_BASE_TOPIC cannot be empty")

        ca_raw = os.getenv("MQTT_CA_FILE", "").strip()
        return cls(
            mqtt_host=os.getenv("MQTT_HOST", "localhost").strip(),
            mqtt_port=_env_int("MQTT_PORT", 1883, 1, 65535),
            mqtt_username=os.getenv("MQTT_USERNAME") or None,
            mqtt_password=os.getenv("MQTT_PASSWORD") or None,
            mqtt_tls=_env_bool("MQTT_TLS", False),
            mqtt_ca_file=Path(ca_raw) if ca_raw else None,
            base_topic=base_topic,
            archiver_id=archiver_id,
            database_path=Path(os.getenv("DATABASE_PATH", "/data/equine-history.sqlite3")),
            retention_days=_env_int("RETENTION_DAYS", 10, 1, 3650),
            tracker_hashes=hashes,
            response_chunk_points=_env_int("RESPONSE_CHUNK_POINTS", 25, 1, 100),
            maximum_request_points=_env_int("MAXIMUM_REQUEST_POINTS", 500, 1, 5000),
        )
