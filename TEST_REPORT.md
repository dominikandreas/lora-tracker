# Packaging validation report

Date: 2026-07-15

## Passed

- Archiver: `python -m pytest -q` — 9 tests passed.
- Web app: `npm test` — 3 tests passed.
- Archive source tree contains no `secrets.h`, runtime `.env`, SQLite database,
  Python bytecode, pytest cache, or `node_modules`.
- ZIP integrity was checked after creation.

## Embedded build/simulation coverage

- The brokerless simulator now has an optional `--embedded-suite`. It compiles
  and runs shared tracker and gateway protocol/configuration headers with a
  host C++17 compiler.
- PlatformIO environments are provided for tracker legacy/S3 and gateway V2
  boards. The legacy tracker sketch was compiled through the ESP32 source
  compilation stage using Espressif32 6.7.0 and its pinned libraries.

## Still required before deployment

Complete PlatformIO link/upload checks for every target, plus physical-board,
radio, MQTT broker, and browser validation. The simulator and host contract
suite do not cover those production behaviors.
