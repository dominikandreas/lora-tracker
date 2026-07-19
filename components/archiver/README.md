# LoRa Tracker archiver

An MQTT-to-SQLite service that keeps recent tracker history and serves
paginated history requests over MQTT.

## Features

- Accepts only point JSON schema 2.
- Stores GNSS fix time and uses server receive time for current-schema points
  that explicitly report no valid fix timestamp.
- Deduplicates using stable `point_id`.
- Records receptions from multiple gateways independently.
- Upgrades an existing SQLite database in place.
- Keeps ten days by default using effective observation time.
- Optional allowlist of tracker hashes.
- Chunked MQTT history-schema v2 responses.
- Retained availability and status topics.
- Consistent live backup, integrity checking, guarded restore and dry-run
  retention maintenance commands.
- Runs directly with Python or in Docker.

## Run locally

```bash
python -m venv .venv
. .venv/bin/activate
pip install .
cp .env.example .env
set -a; . ./.env; set +a
python -m lora_tracker_archiver
```

## Database maintenance

Create a consistent backup while the archiver is running; SQLite's backup API
includes committed WAL data without copying `-wal` or `-shm` files:

```bash
python -m lora_tracker_archiver backup \
  --database /data/lora-tracker-history.sqlite3 \
  --output /backups/history-$(date -u +%F).sqlite3
python -m lora_tracker_archiver check \
  --database /backups/history-$(date -u +%F).sqlite3
```

Retention pruning is a preview unless `--apply` is present:

```bash
python -m lora_tracker_archiver prune --retention-days 30
python -m lora_tracker_archiver prune --retention-days 30 --apply
```

Stop the archiver before restoring. Restoring over an existing database
requires `--force` and automatically creates a timestamped, integrity-checked
`*.pre-restore-*` snapshot first:

```bash
python -m lora_tracker_archiver restore \
  --database /data/lora-tracker-history.sqlite3 \
  --input /backups/history-2026-07-19.sqlite3 \
  --force
```

Every maintenance command emits one machine-readable JSON result and exits
nonzero on validation or safety failures. Exercise backup and restore on a
staging copy before relying on the runbook.

## Run the end-to-end simulator

The brokerless simulator models tracker telemetry, two gateway receptions per
point, MQTT topic/payload validation, archiving, cross-gateway deduplication,
and paginated history responses. It uses the production protocol and SQLite
store, so it can be run before hardware or a broker are available.

```bash
python -m lora_tracker_archiver.simulator --trackers 2 --points 12 --service-suite --embedded-suite
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
mosquitto_sub -t 'lora-tracker/v1/trackers/3db3edf61a18fac0/history/response/demo-1'
```

```bash
mosquitto_pub \
  -t 'lora-tracker/v1/trackers/3db3edf61a18fac0/history/request' \
  -m '{"api_version":1,"schema_version":2,"request_id":"demo-1","limit":100}'
```

## Security

Use broker authentication, ACLs and TLS. End-to-end payload encryption is a
later protocol step; the broker and archiver currently see plaintext telemetry.
