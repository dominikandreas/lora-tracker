"""Environment-backed archiver configuration."""

from __future__ import annotations

from dataclasses import dataclass
import os
from pathlib import Path


def _env_bool(name: str, default: bool = False) -> bool:
    raw = os.getenv(name)
    if raw is None:
        return default
    normalized = raw.strip().lower()
    if normalized in {"1", "true", "yes", "on"}:
        return True
    if normalized in {"0", "false", "no", "off"}:
        return False
    raise ValueError(f"{name} must be a boolean value")


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
        if (
            not 1 <= len(archiver_id) <= 32
            or any(c not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_" for c in archiver_id)
        ):
            raise ValueError(
                "ARCHIVER_ID must be 1-32 letters, digits, '-' or '_'"
            )

        base_topic = os.getenv("MQTT_BASE_TOPIC", "lora-tracker").strip().strip("/")
        if not base_topic or any(char in base_topic for char in "+#\x00"):
            raise ValueError("MQTT_BASE_TOPIC cannot be empty or contain wildcards")

        ca_raw = os.getenv("MQTT_CA_FILE", "").strip()
        mqtt_tls = _env_bool("MQTT_TLS", True)
        if not mqtt_tls and not _env_bool("ALLOW_INSECURE_MQTT", False):
            raise ValueError(
                "Plaintext MQTT is disabled; enable MQTT_TLS or explicitly set "
                "ALLOW_INSECURE_MQTT=true for an isolated development broker"
            )
        ca_file = Path(ca_raw) if ca_raw else None
        if ca_file is not None and not ca_file.is_file():
            raise ValueError(f"MQTT_CA_FILE does not exist: {ca_file}")
        mqtt_host = os.getenv("MQTT_HOST", "localhost").strip()
        if not mqtt_host:
            raise ValueError("MQTT_HOST cannot be empty")
        return cls(
            mqtt_host=mqtt_host,
            mqtt_port=_env_int("MQTT_PORT", 8883 if mqtt_tls else 1883, 1, 65535),
            mqtt_username=os.getenv("MQTT_USERNAME") or None,
            mqtt_password=os.getenv("MQTT_PASSWORD") or None,
            mqtt_tls=mqtt_tls,
            mqtt_ca_file=ca_file,
            base_topic=base_topic,
            archiver_id=archiver_id,
            database_path=Path(os.getenv("DATABASE_PATH", "/data/lora-tracker-history.sqlite3")),
            retention_days=_env_int("RETENTION_DAYS", 10, 1, 3650),
            tracker_hashes=hashes,
            response_chunk_points=_env_int("RESPONSE_CHUNK_POINTS", 25, 1, 100),
            maximum_request_points=_env_int("MAXIMUM_REQUEST_POINTS", 500, 1, 5000),
        )
