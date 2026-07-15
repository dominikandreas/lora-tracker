from lora_tracker_archiver.protocol import (
    ProtocolError,
    iter_history_responses,
    parse_history_request,
    tracker_hash_from_topic,
    validate_point,
)

HASH = "3db3edf61a18fac0"


def point(seq: int = 1, schema: int = 2):
    data = {
        "api_version": 1,
        "point_schema_version": schema,
        "transport_version": 1,
        "schema_version": 2,
        "device_hash": HASH,
        "point_id": f"{HASH}:5:{seq}",
        "latitude": 50.1,
        "longitude": 8.6,
        "dist_m": 42,
        "battery_level": 80,
        "rssi": -100,
        "boot_id": 5,
        "seq": seq,
        "device_id": "wera",
        "device_name": "Wera",
        "gateway_id": "home",
        "gateway_hash": "1111111111111111",
        "gateway_uptime_ms": 1000,
    }
    if schema == 2:
        data.update(
            timestamp_valid=True,
            fix_time_unix_ms=1_784_050_000_000 + seq * 60_000,
            time_source="gnss",
        )
    return data


def test_point_and_topic_validation():
    topic = f"lora-tracker/v1/trackers/{HASH}/events/point"
    assert tracker_hash_from_topic(topic, "events/point") == HASH
    normalized = validate_point(point(), HASH)
    assert normalized["seq"] == 1
    assert normalized["timestamp_valid"] is True


def test_old_point_schema_is_rejected():
    try:
        validate_point(point(schema=1), HASH)
    except ProtocolError as exc:
        assert exc.code == "unsupported_version"
    else:
        raise AssertionError("old point schema accepted")


def test_invalid_timestamp_rejected():
    invalid = point()
    invalid["timestamp_valid"] = False
    try:
        validate_point(invalid, HASH)
    except ProtocolError as exc:
        assert exc.code == "invalid_point"
    else:
        raise AssertionError("inconsistent timestamp accepted")


def test_topic_payload_hash_mismatch_rejected():
    try:
        validate_point(point(), "1111111111111111")
    except ProtocolError as exc:
        assert exc.code == "topic_payload_mismatch"
    else:
        raise AssertionError("mismatch accepted")


def test_inconsistent_point_id_is_rejected():
    invalid = point()
    invalid["point_id"] = f"{HASH}:5:99"
    try:
        validate_point(invalid, HASH)
    except ProtocolError as exc:
        assert exc.code == "invalid_point"
    else:
        raise AssertionError("inconsistent point identity accepted")


def test_history_request_and_chunks():
    request = parse_history_request(
        {
            "api_version": 1,
            "schema_version": 2,
            "request_id": "app-42",
            "limit": 3,
            "cursor": 0,
        },
        500,
    )
    responses = list(
        iter_history_responses(
            request=request,
            device_hash=HASH,
            points=[point(1), point(2), point(3)],
            has_more=True,
            next_cursor=9,
            chunk_size=2,
        )
    )
    assert len(responses) == 2
    assert responses[0]["final"] is False
    assert responses[1]["final"] is True
    assert responses[1]["has_more"] is True
    assert responses[1]["next_cursor"] == 9
    assert responses[1]["schema_version"] == 2


def test_old_history_schema_is_rejected():
    try:
        parse_history_request(
            {"api_version": 1, "schema_version": 1, "request_id": "old"}, 500
        )
    except ProtocolError as exc:
        assert exc.code == "unsupported_version"
    else:
        raise AssertionError("old history schema accepted")
