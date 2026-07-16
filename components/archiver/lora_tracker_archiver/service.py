"""MQTT archiver service."""

from __future__ import annotations

import json
import hashlib
import logging
import signal
import threading
import time
from typing import Any

from .config import ArchiverConfig
from .protocol import (
    API_VERSION,
    HISTORY_SCHEMA_VERSION,
    ProtocolError,
    archiver_availability_topic,
    archiver_status_topic,
    decode_json,
    history_request_subscription,
    history_response_topic,
    gateway_archive_ack_topic,
    iter_history_responses,
    make_error_response,
    parse_history_request,
    point_event_subscription,
    tracker_hash_from_topic,
    validate_point,
    validate_request_id,
)
from .store import HistoryStore

LOG = logging.getLogger("lora_tracker.archiver")


class ArchiverService:
    def __init__(self, config: ArchiverConfig, mqtt_client: Any):
        self.config = config
        self.client = mqtt_client
        self.store = HistoryStore(config.database_path)
        self.started_at_ms = int(time.time() * 1000)
        self.points_received = 0
        self.points_inserted = 0
        self.requests_served = 0
        self._stop = threading.Event()
        self._last_purge_monotonic = 0.0

        self.availability_topic = archiver_availability_topic(config.base_topic, config.archiver_id)
        self.status_topic = archiver_status_topic(config.base_topic, config.archiver_id)
        self.point_subscription = point_event_subscription(config.base_topic)
        self.history_subscription = history_request_subscription(config.base_topic)

    def configure_client(self) -> None:
        self.client.on_connect = self._on_connect
        self.client.on_message = self._on_message
        self.client.will_set(self.availability_topic, "offline", qos=1, retain=True)
        if self.config.mqtt_username:
            self.client.username_pw_set(self.config.mqtt_username, self.config.mqtt_password)
        if self.config.mqtt_tls:
            self.client.tls_set(ca_certs=str(self.config.mqtt_ca_file) if self.config.mqtt_ca_file else None)

    def _on_connect(self, client: Any, userdata: Any, flags: Any, reason_code: Any, properties: Any = None) -> None:
        if int(reason_code) != 0:
            LOG.error("MQTT connection failed: %s", reason_code)
            return
        LOG.info("Connected to MQTT broker")
        client.subscribe(self.point_subscription, qos=1)
        client.subscribe(self.history_subscription, qos=1)
        client.publish(self.availability_topic, "online", qos=1, retain=True)
        self.publish_status()

    def _tracker_allowed(self, device_hash: str) -> bool:
        return not self.config.tracker_hashes or device_hash in self.config.tracker_hashes

    def _on_message(self, client: Any, userdata: Any, message: Any) -> None:
        try:
            if message.topic.endswith("/events/point"):
                self._handle_point(message.topic, message.payload)
            elif message.topic.endswith("/history/request"):
                self._handle_history_request(message.topic, message.payload)
        except Exception:
            LOG.exception("Unhandled message failure for topic %s", message.topic)

    def _handle_point(self, topic: str, payload: bytes) -> None:
        device_hash = tracker_hash_from_topic(topic, "events/point", self.config.base_topic)
        if not self._tracker_allowed(device_hash):
            return
        self.points_received += 1
        point = validate_point(decode_json(payload), device_hash)
        inserted = self.store.insert_point(point, int(time.time() * 1000))
        if inserted:
            self.points_inserted += 1
            LOG.debug("Stored point %s", point["point_id"])
        else:
            LOG.debug("Ignored duplicate point %s", point["point_id"])
        ack_topic = gateway_archive_ack_topic(
            self.config.base_topic, point["gateway_hash"]
        )
        result = self.client.publish(
            ack_topic, point["point_id"], qos=1, retain=False
        )
        if hasattr(result, "rc") and result.rc != 0:
            LOG.error(
                "Archive confirmation publish failed for %s with rc=%s",
                point["point_id"], result.rc,
            )

    def _publish_history_payload(self, topic: str, payload: dict[str, Any]) -> None:
        encoded = json.dumps(payload, separators=(",", ":"), ensure_ascii=False)
        result = self.client.publish(topic, encoded, qos=1, retain=False)
        if hasattr(result, "rc") and result.rc != 0:
            LOG.error("MQTT history response publish failed with rc=%s", result.rc)

    def _handle_history_request(self, topic: str, payload: bytes) -> None:
        device_hash = tracker_hash_from_topic(topic, "history/request", self.config.base_topic)
        request_id: str | None = None
        try:
            data = decode_json(payload)
            raw_request_id = data.get("request_id")
            request_id = raw_request_id if isinstance(raw_request_id, str) else None
            if not self._tracker_allowed(device_hash):
                raise ProtocolError("tracker_not_archived", "this archiver is not configured for the requested tracker")
            request = parse_history_request(data, self.config.maximum_request_points)
            response_topic = history_response_topic(self.config.base_topic, device_hash, request.request_id)
            result = self.store.query(
                device_hash=device_hash,
                from_unix_ms=request.from_unix_ms,
                to_unix_ms=request.to_unix_ms,
                limit=request.limit,
                cursor=request.cursor,
            )
            for response in iter_history_responses(
                request=request,
                device_hash=device_hash,
                points=result.points,
                has_more=result.has_more,
                next_cursor=result.next_cursor,
                chunk_size=self.config.response_chunk_points,
            ):
                self._publish_history_payload(response_topic, response)
            self.requests_served += 1
        except ProtocolError as exc:
            try:
                safe_request_id = validate_request_id(request_id)
            except ProtocolError:
                safe_request_id = "invalid"
            response_topic = history_response_topic(self.config.base_topic, device_hash, safe_request_id)
            self._publish_history_payload(
                response_topic,
                make_error_response(safe_request_id, device_hash, exc.code, exc.message),
            )

    def publish_status(self) -> None:
        now_ms = int(time.time() * 1000)
        payload = {
            "api_version": API_VERSION,
            "schema_version": 1,
            "archiver_id": self.config.archiver_id,
            "uptime_s": max(0, (now_ms - self.started_at_ms) // 1000),
            "stored_points": self.store.count(),
            "points_received": self.points_received,
            "points_inserted": self.points_inserted,
            "requests_served": self.requests_served,
            "retention_days": self.config.retention_days,
            "tracker_filter_count": len(self.config.tracker_hashes),
        }
        self.client.publish(
            self.status_topic,
            json.dumps(payload, separators=(",", ":")),
            qos=1,
            retain=True,
        )

    def maintenance(self) -> None:
        now = time.monotonic()
        if now - self._last_purge_monotonic >= 3600:
            cutoff = int((time.time() - self.config.retention_days * 86400) * 1000)
            removed = self.store.purge_before(cutoff)
            if removed:
                LOG.info("Purged %d expired points", removed)
            self._last_purge_monotonic = now
        self.publish_status()

    def request_stop(self, *_: Any) -> None:
        self._stop.set()

    def run(self) -> None:
        self.configure_client()
        signal.signal(signal.SIGINT, self.request_stop)
        signal.signal(signal.SIGTERM, self.request_stop)
        self.client.connect(self.config.mqtt_host, self.config.mqtt_port, keepalive=60)
        self.client.loop_start()
        try:
            while not self._stop.wait(60):
                self.maintenance()
        finally:
            self.client.publish(self.availability_topic, "offline", qos=1, retain=True)
            self.client.disconnect()
            self.client.loop_stop()
            self.store.close()


def make_mqtt_client(config: ArchiverConfig) -> Any:
    try:
        import paho.mqtt.client as mqtt
    except ImportError as exc:
        raise RuntimeError("paho-mqtt is required; install the project dependencies") from exc

    identity_hash = hashlib.sha256(config.archiver_id.encode()).hexdigest()[:8]
    client_id = f"lta-{config.archiver_id[:8]}-{identity_hash}"
    return mqtt.Client(
        callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
        client_id=client_id,
        protocol=mqtt.MQTTv311,
    )
