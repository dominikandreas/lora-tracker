"""Strict MQTT topic and payload validation."""

from __future__ import annotations

from dataclasses import dataclass
import json
import re
from typing import Any, Iterable

API_VERSION = 1
POINT_SCHEMA_VERSION = 2
HISTORY_SCHEMA_VERSION = 2

_DEVICE_HASH_RE = re.compile(r"^[0-9a-f]{16}$")
_CANONICAL_ID_RE = re.compile(r"^[a-z0-9](?:[a-z0-9_-]{0,23})$")
_REQUEST_ID_RE = re.compile(r"^[A-Za-z0-9_.-]{1,48}$")
_MAX_UNIX_MS = 32_503_680_000_000  # year 3000


class ProtocolError(ValueError):
    def __init__(self, code: str, message: str):
        super().__init__(message)
        self.code = code
        self.message = message


def validate_device_hash(value: str) -> str:
    normalized = value.lower()
    if not _DEVICE_HASH_RE.fullmatch(normalized):
        raise ProtocolError(
            "invalid_device_hash",
            "device_hash must be 16 lowercase hexadecimal characters",
        )
    return normalized


def validate_request_id(value: Any) -> str:
    if not isinstance(value, str) or not _REQUEST_ID_RE.fullmatch(value):
        raise ProtocolError(
            "invalid_request_id",
            "request_id contains unsupported characters or length",
        )
    return value


def point_event_subscription(base_topic: str) -> str:
    return f"{base_topic}/v{API_VERSION}/trackers/+/events/point"


def history_request_subscription(base_topic: str) -> str:
    return f"{base_topic}/v{API_VERSION}/trackers/+/history/request"


def history_response_topic(base_topic: str, device_hash: str, request_id: str) -> str:
    return (
        f"{base_topic}/v{API_VERSION}/trackers/{device_hash}/"
        f"history/response/{request_id}"
    )


def archiver_availability_topic(base_topic: str, archiver_id: str) -> str:
    return f"{base_topic}/v{API_VERSION}/archivers/{archiver_id}/availability"


def archiver_status_topic(base_topic: str, archiver_id: str) -> str:
    return f"{base_topic}/v{API_VERSION}/archivers/{archiver_id}/status"


def tracker_hash_from_topic(topic: str, expected_suffix: str) -> str:
    parts = topic.split("/")
    try:
        version_index = parts.index(f"v{API_VERSION}")
    except ValueError as exc:
        raise ProtocolError(
            "invalid_topic", "topic has no supported API version"
        ) from exc
    tail = parts[version_index:]
    expected = [f"v{API_VERSION}", "trackers", "<hash>"] + expected_suffix.split("/")
    if len(tail) != len(expected) or tail[1] != "trackers" or tail[3:] != expected[3:]:
        raise ProtocolError(
            "invalid_topic", "topic does not match the expected tracker route"
        )
    return validate_device_hash(tail[2])


def decode_json(payload: bytes | str) -> dict[str, Any]:
    try:
        text = payload.decode("utf-8") if isinstance(payload, bytes) else payload
        value = json.loads(text)
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ProtocolError(
            "invalid_json", "payload is not valid UTF-8 JSON"
        ) from exc
    if not isinstance(value, dict):
        raise ProtocolError("invalid_json", "payload must be a JSON object")
    return value


def _require_int(
    data: dict[str, Any], key: str, minimum: int, maximum: int
) -> int:
    value = data.get(key)
    if (
        isinstance(value, bool)
        or not isinstance(value, int)
        or not minimum <= value <= maximum
    ):
        raise ProtocolError(
            "invalid_point",
            f"{key} must be an integer between {minimum} and {maximum}",
        )
    return value


def _require_float(
    data: dict[str, Any], key: str, minimum: float, maximum: float
) -> float:
    value = data.get(key)
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ProtocolError("invalid_point", f"{key} must be numeric")
    result = float(value)
    if not minimum <= result <= maximum:
        raise ProtocolError("invalid_point", f"{key} is out of range")
    return result


def validate_point(data: dict[str, Any], topic_device_hash: str) -> dict[str, Any]:
    schema_version = data.get("point_schema_version")
    if (
        data.get("api_version") != API_VERSION
        or schema_version != POINT_SCHEMA_VERSION
    ):
        raise ProtocolError(
            "unsupported_version", "unsupported point API or schema version"
        )
    if data.get("transport_version") != 2 or data.get("schema_version") != 2:
        raise ProtocolError(
            "unsupported_version", "unsupported LoRa transport or history schema"
        )
    device_hash = validate_device_hash(str(data.get("device_hash", "")))
    if device_hash != topic_device_hash:
        raise ProtocolError(
            "topic_payload_mismatch", "device hash does not match MQTT topic"
        )

    point_id = data.get("point_id")
    if not isinstance(point_id, str) or len(point_id) > 96:
        raise ProtocolError("invalid_point", "point_id is missing or inconsistent")

    normalized = dict(data)
    normalized["device_hash"] = device_hash
    normalized["point_id"] = point_id
    normalized["latitude"] = _require_float(data, "latitude", -90.0, 90.0)
    normalized["longitude"] = _require_float(data, "longitude", -180.0, 180.0)
    normalized["dist_m"] = _require_int(data, "dist_m", 0, 4_294_967_295)
    normalized["battery_level"] = _require_int(data, "battery_level", 0, 100)
    normalized["rssi"] = _require_int(data, "rssi", -200, 50)
    normalized["boot_id"] = _require_int(data, "boot_id", 0, 4_294_967_295)
    normalized["seq"] = _require_int(data, "seq", 0, 4_294_967_295)
    if point_id != f'{device_hash}:{normalized["boot_id"]}:{normalized["seq"]}':
        raise ProtocolError("invalid_point", "point_id does not match identity and sequence")

    for key in ("device_id", "gateway_id"):
        value = normalized.get(key)
        if not isinstance(value, str) or not _CANONICAL_ID_RE.fullmatch(value):
            raise ProtocolError("invalid_point", f"{key} is not a canonical identifier")
    for key in ("device_name",):
        value = normalized.get(key)
        if (
            not isinstance(value, str)
            or not 1 <= len(value) <= 32
            or any(ord(char) < 0x20 or ord(char) == 0x7F for char in value)
        ):
            raise ProtocolError("invalid_point", f"{key} is not a valid display name")
    normalized["gateway_hash"] = validate_device_hash(
        str(normalized.get("gateway_hash", ""))
    )
    normalized["gateway_uptime_ms"] = _require_int(
        data, "gateway_uptime_ms", 0, 4_294_967_295
    )

    timestamp_valid = data.get("timestamp_valid")
    if not isinstance(timestamp_valid, bool):
        raise ProtocolError("invalid_point", "timestamp_valid must be boolean")
    fix_time = _require_int(data, "fix_time_unix_ms", 0, _MAX_UNIX_MS)
    source = data.get("time_source")
    if source not in {"gnss", "unavailable"}:
        raise ProtocolError("invalid_point", "unsupported time_source")
    if timestamp_valid and (fix_time == 0 or source != "gnss"):
        raise ProtocolError("invalid_point", "valid timestamps require GNSS time")
    if not timestamp_valid and fix_time != 0:
        raise ProtocolError("invalid_point", "invalid timestamps must use zero epoch")
    normalized["timestamp_valid"] = timestamp_valid
    normalized["fix_time_unix_ms"] = fix_time
    normalized["time_source"] = source

    return normalized


@dataclass(frozen=True)
class HistoryRequest:
    request_id: str
    from_unix_ms: int
    to_unix_ms: int
    limit: int
    cursor: int
    schema_version: int


def parse_history_request(
    data: dict[str, Any], maximum_points: int
) -> HistoryRequest:
    schema_version = data.get("schema_version")
    if (
        data.get("api_version") != API_VERSION
        or schema_version != HISTORY_SCHEMA_VERSION
    ):
        raise ProtocolError(
            "unsupported_version", "unsupported history API or schema version"
        )
    request_id = validate_request_id(data.get("request_id"))

    from_ms = data.get("from_unix_ms", 0)
    to_ms = data.get("to_unix_ms", _MAX_UNIX_MS)
    cursor = data.get("cursor", 0)
    limit = data.get("limit", min(250, maximum_points))
    for key, value, minimum, maximum in (
        ("from_unix_ms", from_ms, 0, _MAX_UNIX_MS),
        ("to_unix_ms", to_ms, 0, _MAX_UNIX_MS),
        ("cursor", cursor, 0, 9_223_372_036_854_775_807),
        ("limit", limit, 1, maximum_points),
    ):
        if (
            isinstance(value, bool)
            or not isinstance(value, int)
            or not minimum <= value <= maximum
        ):
            raise ProtocolError(
                "invalid_history_request", f"{key} is outside its allowed range"
            )
    if from_ms > to_ms:
        raise ProtocolError(
            "invalid_history_request", "from_unix_ms must not exceed to_unix_ms"
        )
    return HistoryRequest(
        request_id, from_ms, to_ms, limit, cursor, int(schema_version)
    )


def make_error_response(
    request_id: str | None,
    device_hash: str,
    code: str,
    message: str,
    schema_version: int = HISTORY_SCHEMA_VERSION,
) -> dict[str, Any]:
    return {
        "api_version": API_VERSION,
        "schema_version": schema_version,
        "request_id": request_id or "invalid",
        "device_hash": device_hash,
        "ok": False,
        "error": code,
        "message": message,
        "final": True,
    }


def iter_history_responses(
    *,
    request: HistoryRequest,
    device_hash: str,
    points: list[dict[str, Any]],
    has_more: bool,
    next_cursor: int,
    chunk_size: int,
) -> Iterable[dict[str, Any]]:
    if not points:
        yield {
            "api_version": API_VERSION,
            "schema_version": request.schema_version,
            "request_id": request.request_id,
            "device_hash": device_hash,
            "ok": True,
            "chunk_index": 0,
            "final": True,
            "has_more": False,
            "next_cursor": request.cursor,
            "points": [],
        }
        return

    chunks = [points[i : i + chunk_size] for i in range(0, len(points), chunk_size)]
    for index, chunk in enumerate(chunks):
        final = index == len(chunks) - 1
        yield {
            "api_version": API_VERSION,
            "schema_version": request.schema_version,
            "request_id": request.request_id,
            "device_hash": device_hash,
            "ok": True,
            "chunk_index": index,
            "final": final,
            "has_more": has_more if final else True,
            "next_cursor": next_cursor if final else 0,
            "points": chunk,
        }
