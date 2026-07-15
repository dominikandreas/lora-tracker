"""Deterministic, brokerless end-to-end simulator for LoRa Tracker.

The simulator models the messages at the boundaries between tracker, gateway,
archiver, and web app.  It deliberately uses the archiver's production protocol
and SQLite store, so it is useful for repeatable integration checks without
LoRa boards or an MQTT broker.
"""

from __future__ import annotations

from dataclasses import asdict, dataclass
import argparse
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
from typing import Iterable

from .config import ArchiverConfig

from .protocol import (
    iter_history_responses,
    parse_history_request,
    tracker_hash_from_topic,
    validate_point,
)
from .store import HistoryStore
from .service import ArchiverService


@dataclass(frozen=True)
class SimulationSummary:
    trackers: int
    points_per_tracker: int
    tracker_points_emitted: int
    gateway_events_published: int
    points_archived: int
    duplicate_receptions: int
    history_responses: int
    history_points_returned: int
    database: str


@dataclass(frozen=True)
class PublishedMessage:
    topic: str
    payload: str
    qos: int
    retain: bool


def run_embedded_simulation(*, compiler: str | None = None) -> dict[str, object]:
    """Compile and run shared tracker and gateway C++ contracts on the host.

    The harness uses a deliberately small Arduino compatibility header, so it
    executes production protocol/configuration code but makes no claim about
    ESP32 peripherals or radio hardware.
    """
    repository = Path(__file__).resolve().parents[3]
    harness = repository / "components" / "firmware-simulator"
    if compiler is None:
        compiler = os.environ.get("CXX")
    if compiler is None:
        compiler = next(
            (candidate for candidate in ("g++", "clang++", "c++") if shutil.which(candidate)),
            None,
        )
    compiler_parts = shlex.split(compiler) if compiler else []
    if not compiler_parts or shutil.which(compiler_parts[0]) is None:
        raise RuntimeError(
            "embedded simulation requires a C++17 compiler; install g++ or set CXX"
        )

    source = harness / "firmware_contract_test.cpp"
    results: dict[str, object] = {"compiler": compiler, "components": []}
    with tempfile.TemporaryDirectory(prefix="lora-tracker-firmware-sim-") as build_dir:
        for component in ("tracker-firmware", "gateway-firmware"):
            output = Path(build_dir) / component
            command = [
                *compiler_parts, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                "-I", str(harness / "include"),
                "-I", str(repository / "components" / component),
                str(source), "-o", str(output),
            ]
            subprocess.run(command, check=True, capture_output=True, text=True)
            subprocess.run([str(output)], check=True, capture_output=True, text=True)
            results["components"].append(component)
    return results


def _reset_simulation_database(database: Path) -> None:
    """Make a simulator output deterministic when its database path is reused."""
    database.parent.mkdir(parents=True, exist_ok=True)
    for suffix in ("", "-shm", "-wal"):
        database.with_name(f"{database.name}{suffix}").unlink(missing_ok=True)


class InMemoryMqttClient:
    """Small MQTT client double used to exercise ArchiverService end to end.

    It records QoS/retain flags, subscriptions, last-will setup, and can deliver
    broker messages into the production service without opening a socket.
    """
    def __init__(self) -> None:
        self.on_connect = None
        self.on_message = None
        self.subscriptions: list[tuple[str, int]] = []
        self.published: list[PublishedMessage] = []
        self.will: tuple[str, str, int, bool] | None = None
        self.credentials: tuple[str | None, str | None] | None = None
        self.tls_ca_file: str | None = None

    def will_set(self, topic: str, payload: str, qos: int, retain: bool) -> None:
        self.will = (topic, payload, qos, retain)

    def username_pw_set(self, username: str | None, password: str | None) -> None:
        self.credentials = (username, password)

    def tls_set(self, ca_certs: str | None = None) -> None:
        self.tls_ca_file = ca_certs

    def subscribe(self, topic: str, qos: int = 0) -> None:
        self.subscriptions.append((topic, qos))

    def publish(self, topic: str, payload: str, qos: int = 0, retain: bool = False):
        self.published.append(PublishedMessage(topic, payload, qos, retain))
        return type("PublishResult", (), {"rc": 0})()

    def deliver(self, topic: str, payload: dict[str, object] | str | bytes) -> None:
        if self.on_message is None:
            raise RuntimeError("client has not been configured")
        encoded = json.dumps(payload).encode() if isinstance(payload, dict) else (
            payload.encode() if isinstance(payload, str) else payload
        )
        message = type("MqttMessage", (), {"topic": topic, "payload": encoded})()
        self.on_message(self, None, message)


def _device_hash(index: int) -> str:
    # Fixed 64-bit identifiers make test results reproducible.
    return f"{0x3DB3EDF61A18FAC0 + index:016x}"


def _tracker_points(
    *, device_hash: str, tracker_index: int, points: int, start_ms: int
) -> Iterable[dict[str, object]]:
    """Generate a small, believable route as tracker-originated telemetry."""
    for seq in range(points):
        # Make every fifth sample untimed to exercise receive-time fallback.
        timestamp_valid = seq % 5 != 4
        yield {
            "api_version": 1,
            "point_schema_version": 2,
            "transport_version": 2,
            "schema_version": 2,
            "device_id": f"horse-{tracker_index + 1}",
            "device_name": f"Simulated Horse {tracker_index + 1}",
            "device_hash": device_hash,
            "point_id": f"{device_hash}:1:{seq}",
            "latitude": 50.228470 + tracker_index * 0.01 + seq * 0.00021,
            "longitude": 8.564520 + tracker_index * 0.01 + seq * 0.00016,
            "dist_m": seq * 27,
            "battery_level": max(0, 95 - seq),
            "rssi": -112 + (seq % 9),
            "seq": seq,
            "boot_id": 1,
            "timestamp_valid": timestamp_valid,
            "fix_time_unix_ms": start_ms + seq * 60_000 if timestamp_valid else 0,
            "time_source": "gnss" if timestamp_valid else "unavailable",
        }


def _gateway_event(point: dict[str, object], gateway_index: int) -> dict[str, object]:
    """Apply the gateway-owned reception metadata to a tracker packet."""
    event = dict(point)
    event["gateway_id"] = ("home", "field")[gateway_index]
    event["gateway_hash"] = ("1111111111111111", "2222222222222222")[gateway_index]
    event["rssi"] = int(point["rssi"]) + gateway_index * 7
    event["gateway_uptime_ms"] = 60_000 + int(point["seq"]) * 60_000
    return event


def run_simulation(
    *,
    database: Path | str,
    tracker_count: int = 2,
    points_per_tracker: int = 12,
    base_topic: str = "lora-tracker",
    start_unix_ms: int = 1_784_050_000_000,
) -> SimulationSummary:
    """Run tracker -> gateway -> MQTT event -> archiver -> history flow.

    Each point is received by two virtual gateways.  The second reception must
    be recorded while the point itself is deduplicated.  A history request is
    then served in small chunks, as the PWA consumes it.
    """
    if tracker_count < 1 or points_per_tracker < 1:
        raise ValueError("tracker_count and points_per_tracker must be positive")

    database = Path(database)
    _reset_simulation_database(database)
    emitted = gateway_events = archived = duplicates = response_count = returned = 0
    store = HistoryStore(database)
    try:
        for tracker_index in range(tracker_count):
            device_hash = _device_hash(tracker_index)
            topic = f"{base_topic}/v1/trackers/{device_hash}/events/point"
            for point in _tracker_points(
                device_hash=device_hash,
                tracker_index=tracker_index,
                points=points_per_tracker,
                start_ms=start_unix_ms,
            ):
                emitted += 1
                for gateway_index in range(2):
                    event = _gateway_event(point, gateway_index)
                    # This is the same validation the service performs after a
                    # broker delivers a MQTT point event.
                    topic_hash = tracker_hash_from_topic(topic, "events/point")
                    normalized = validate_point(event, topic_hash)
                    gateway_events += 1
                    received_at_ms = start_unix_ms + int(point["seq"]) * 60_000 + gateway_index
                    if store.insert_point(normalized, received_at_ms):
                        archived += 1
                    else:
                        duplicates += 1

            request = parse_history_request(
                {
                    "api_version": 1,
                    "schema_version": 2,
                    "request_id": f"sim-{tracker_index + 1}",
                    "from_unix_ms": start_unix_ms - 1,
                    "to_unix_ms": start_unix_ms + points_per_tracker * 60_000,
                    "limit": points_per_tracker,
                    "cursor": 0,
                },
                maximum_points=500,
            )
            page = store.query(
                device_hash=device_hash,
                from_unix_ms=request.from_unix_ms,
                to_unix_ms=request.to_unix_ms,
                limit=request.limit,
                cursor=request.cursor,
            )
            responses = list(
                iter_history_responses(
                    request=request,
                    device_hash=device_hash,
                    points=page.points,
                    has_more=page.has_more,
                    next_cursor=page.next_cursor,
                    chunk_size=5,
                )
            )
            if not responses or not responses[-1]["final"]:
                raise RuntimeError("history response did not terminate")
            response_count += len(responses)
            returned += sum(len(response["points"]) for response in responses)
    finally:
        store.close()

    return SimulationSummary(
        trackers=tracker_count,
        points_per_tracker=points_per_tracker,
        tracker_points_emitted=emitted,
        gateway_events_published=gateway_events,
        points_archived=archived,
        duplicate_receptions=duplicates,
        history_responses=response_count,
        history_points_returned=returned,
        database=str(database.resolve()),
    )


def run_service_simulation(*, database: Path | str) -> dict[str, object]:
    """Exercise service setup, MQTT dispatch, filtering, errors and pagination."""
    database = Path(database)
    _reset_simulation_database(database)
    allowed_hash = _device_hash(0)
    client = InMemoryMqttClient()
    config = ArchiverConfig(
        mqtt_host="simulator",
        mqtt_port=1883,
        mqtt_username="sim-user",
        mqtt_password="sim-password",
        mqtt_tls=True,
        mqtt_ca_file=Path("sim-ca.pem"),
        base_topic="lora-tracker",
        archiver_id="simulator",
        database_path=database,
        retention_days=10,
        tracker_hashes=frozenset({allowed_hash}),
        response_chunk_points=2,
        maximum_request_points=3,
    )
    service = ArchiverService(config, client)
    try:
        service.configure_client()
        assert client.will == ("lora-tracker/v1/archivers/simulator/availability", "offline", 1, True)
        assert client.credentials == ("sim-user", "sim-password")
        assert client.tls_ca_file == "sim-ca.pem"
        assert client.on_connect is not None
        client.on_connect(client, None, None, 0)
        assert len(client.subscriptions) == 2

        topic = f"lora-tracker/v1/trackers/{allowed_hash}/events/point"
        points = list(_tracker_points(device_hash=allowed_hash, tracker_index=0, points=4, start_ms=1_784_050_000_000))
        for point in points:
            client.deliver(topic, _gateway_event(point, 0))
        client.deliver(topic, _gateway_event(points[0], 1))  # duplicate reception
        # A non-allowlisted tracker must not affect counters or storage.
        denied_hash = _device_hash(1)
        denied = _gateway_event(next(_tracker_points(device_hash=denied_hash, tracker_index=1, points=1, start_ms=1)), 0)
        client.deliver(f"lora-tracker/v1/trackers/{denied_hash}/events/point", denied)

        request_topic = f"lora-tracker/v1/trackers/{allowed_hash}/history/request"
        client.deliver(request_topic, {
            "api_version": 1, "schema_version": 2, "request_id": "page-1",
            "from_unix_ms": 0, "to_unix_ms": 32_503_680_000_000, "limit": 3, "cursor": 0,
        })
        client.deliver(request_topic, {
            "api_version": 1, "schema_version": 2, "request_id": "bad-range",
            "from_unix_ms": 2, "to_unix_ms": 1,
        })
        responses = [json.loads(message.payload) for message in client.published if "/history/response/" in message.topic]
        successful = [response for response in responses if response["ok"]]
        errors = [response for response in responses if not response["ok"]]
        assert len(successful) == 2 and successful[-1]["final"] and successful[-1]["has_more"]
        assert errors and errors[-1]["error"] == "invalid_history_request"
        assert service.points_received == 5 and service.points_inserted == 4
        return {
            "subscriptions": len(client.subscriptions),
            "stored_points": service.store.count(),
            "points_received": service.points_received,
            "points_inserted": service.points_inserted,
            "history_chunks": len(successful),
            "history_error_responses": len(errors),
        }
    finally:
        service.store.close()


def main() -> None:
    parser = argparse.ArgumentParser(description="Run the LoRa Tracker end-to-end simulator")
    parser.add_argument("--trackers", type=int, default=2, help="number of simulated trackers")
    parser.add_argument("--points", type=int, default=12, help="points emitted per tracker")
    parser.add_argument("--database", type=Path, help="SQLite output file (temporary by default)")
    parser.add_argument("--base-topic", default="lora-tracker", help="MQTT topic root")
    parser.add_argument("--service-suite", action="store_true", help="also run the ArchiverService MQTT simulation")
    parser.add_argument("--embedded-suite", action="store_true", help="also compile/run tracker and gateway C++ contract tests")
    args = parser.parse_args()
    database = args.database or Path(tempfile.gettempdir()) / "lora-tracker-simulation.sqlite3"
    summary = run_simulation(
        database=database,
        tracker_count=args.trackers,
        points_per_tracker=args.points,
        base_topic=args.base_topic,
    )
    result: dict[str, object] = {"pipeline": asdict(summary)}
    if args.service_suite:
        result["archiver_service"] = run_service_simulation(database=database.with_name(f"{database.stem}-service.sqlite3"))
    if args.embedded_suite:
        result["embedded"] = run_embedded_simulation()
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
