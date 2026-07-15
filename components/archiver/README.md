# LoRa Tracker archiver

An MQTT-to-SQLite service that keeps recent tracker history and serves
paginated history requests over MQTT.

## Features

- Accepts point JSON schema v1 and v2 during rolling upgrades.
- Stores GNSS fix time from schema v2 and falls back to server receive time for
  legacy untimed points.
- Deduplicates using stable `point_id`.
- Records receptions from multiple gateways independently.
- Migrates an existing v1 SQLite database in place.
- Keeps ten days by default using effective observation time.
- Optional allowlist of tracker hashes.
- Chunked MQTT history-schema v2 responses.
- Retained availability and status topics.
- Runs directly with Python or in Docker.

## Run locally

```bash
python -m venv .venv
. .venv/bin/activate
pip install .
cp .env.example .env
set -a; . ./.env; set +a
python -m equine_archiver
```

## Run the end-to-end simulator

The brokerless simulator models tracker telemetry, two gateway receptions per
point, MQTT topic/payload validation, archiving, cross-gateway deduplication,
and paginated history responses. It uses the production protocol and SQLite
store, so it can be run before hardware or a broker are available.

```bash
python -m equine_archiver.simulator --trackers 2 --points 12 --service-suite --embedded-suite
```

It prints a JSON summary and writes a temporary SQLite database by default. Use
`--database simulation.sqlite3` to keep the result for inspection.
`--embedded-suite` requires a host C++17 compiler and compiles/runs shared
tracker and gateway protocol/configuration contracts. It does not build ESP32
sketches; use the adjacent pinned PlatformIO projects for that.
The coverage matrix and hardware/infrastructure boundary are documented in
[`docs/SIMULATION_COVERAGE.md`](../../docs/SIMULATION_COVERAGE.md).

## Docker

```bash
cp .env.example .env
docker compose -f docker-compose.example.yml up --build -d
```

## Request history

```bash
mosquitto_sub -t 'equine/v1/trackers/3db3edf61a18fac0/history/response/demo-1'
```

```bash
mosquitto_pub \
  -t 'equine/v1/trackers/3db3edf61a18fac0/history/request' \
  -m '{"api_version":1,"schema_version":2,"request_id":"demo-1","limit":100}'
```

## Security

Use broker authentication, ACLs and TLS. End-to-end payload encryption is a
later protocol step; the broker and archiver currently see plaintext telemetry.
