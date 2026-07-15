#!/usr/bin/env python3
"""Create ESP Web Tools-compatible merged firmware and manifests."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys


TARGETS = {
    "tracker-v2": {
        "project": "tracker-firmware",
        "environment": "heltec_wifi_lora_32_v2",
        "chip": "esp32",
        "chip_family": "ESP32",
        "bootloader_offset": "0x1000",
        "flash_mode": "dio",
        "flash_freq": "40m",
        "flash_size": "8MB",
    },
    "tracker-wireless-tracker": {
        "project": "tracker-firmware",
        "environment": "heltec_wireless_tracker",
        "chip": "esp32s3",
        "chip_family": "ESP32-S3",
        "bootloader_offset": "0x0",
        "flash_mode": "dio",
        "flash_freq": "80m",
        "flash_size": "8MB",
    },
    "gateway-v2": {
        "project": "gateway-firmware",
        "environment": "heltec_wifi_lora_32_v2",
        "chip": "esp32",
        "chip_family": "ESP32",
        "bootloader_offset": "0x1000",
        "flash_mode": "dio",
        "flash_freq": "40m",
        "flash_size": "8MB",
    },
}


def _boot_app0() -> Path:
    matches = list(
        Path.home().glob(
            ".platformio/packages/framework-arduinoespressif32/"
            "tools/partitions/boot_app0.bin"
        )
    )
    if len(matches) != 1:
        raise SystemExit("could not locate PlatformIO framework boot_app0.bin")
    return matches[0]


def _esptool_command() -> list[str]:
    executable = shutil.which("esptool") or shutil.which("esptool.py")
    if executable:
        return [executable]
    packaged = Path.home() / ".platformio/packages/tool-esptoolpy/esptool.py"
    if packaged.is_file():
        return [sys.executable, str(packaged)]
    raise SystemExit("could not locate esptool; install esptool 4.5.1")


def package(target_name: str, version: str, output: Path, download_base: str) -> None:
    target = TARGETS[target_name]
    root = Path(__file__).resolve().parents[1]
    build = (
        root
        / "components"
        / ".pio-build"
        / str(target["project"])
        / str(target["environment"])
    )
    required = [build / "bootloader.bin", build / "partitions.bin", build / "firmware.bin", build / "firmware.elf"]
    missing = [str(path) for path in required if not path.is_file()]
    if missing:
        raise SystemExit(f"missing build products: {', '.join(missing)}")

    output.mkdir(parents=True, exist_ok=True)
    stem = f"lora-tracker-{target_name}-{version}"
    factory_name = f"{stem}.factory.bin"
    app_name = f"{stem}.app.bin"
    elf_name = f"{stem}.elf"
    manifest_name = f"{stem}.manifest.json"
    factory = output / factory_name

    subprocess.run(
        _esptool_command()
        + [
            "--chip",
            str(target["chip"]),
            "merge_bin",
            "-o",
            str(factory),
            "--flash_mode",
            str(target["flash_mode"]),
            "--flash_freq",
            str(target["flash_freq"]),
            "--flash_size",
            str(target["flash_size"]),
            str(target["bootloader_offset"]),
            str(build / "bootloader.bin"),
            "0x8000",
            str(build / "partitions.bin"),
            "0xe000",
            str(_boot_app0()),
            "0x10000",
            str(build / "firmware.bin"),
        ],
        check=True,
    )
    shutil.copy2(build / "firmware.bin", output / app_name)
    shutil.copy2(build / "firmware.elf", output / elf_name)

    base = download_base.rstrip("/")
    manifest = {
        "name": f"LoRa Tracker {target_name}",
        "version": version,
        "new_install_prompt_erase": True,
        "new_install_improv_wait_time": 0,
        "builds": [
            {
                "chipFamily": target["chip_family"],
                "improv": False,
                "parts": [{"path": f"{base}/{factory_name}", "offset": 0}],
            }
        ],
    }
    (output / manifest_name).write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--target", choices=TARGETS, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--output", type=Path, default=Path("dist"))
    parser.add_argument("--download-base", required=True)
    args = parser.parse_args()
    package(args.target, args.version, args.output, args.download_base)


if __name__ == "__main__":
    main()
