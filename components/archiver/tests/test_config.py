import pytest

from lora_tracker_archiver.config import ArchiverConfig


def test_secure_defaults(monkeypatch, tmp_path):
    for key in (
        "MQTT_TLS",
        "ALLOW_INSECURE_MQTT",
        "MQTT_PORT",
        "MQTT_BASE_TOPIC",
        "DATABASE_PATH",
    ):
        monkeypatch.delenv(key, raising=False)
    monkeypatch.setenv("DATABASE_PATH", str(tmp_path / "history.sqlite3"))
    config = ArchiverConfig.from_env()
    assert config.mqtt_tls is True
    assert config.mqtt_port == 8883
    assert config.base_topic == "lora-tracker"


def test_plaintext_requires_explicit_opt_in(monkeypatch):
    monkeypatch.setenv("MQTT_TLS", "false")
    monkeypatch.setenv("ALLOW_INSECURE_MQTT", "false")
    with pytest.raises(ValueError, match="Plaintext MQTT is disabled"):
        ArchiverConfig.from_env()


def test_invalid_boolean_is_not_silently_disabled(monkeypatch):
    monkeypatch.setenv("MQTT_TLS", "tru")
    with pytest.raises(ValueError, match="boolean"):
        ArchiverConfig.from_env()
