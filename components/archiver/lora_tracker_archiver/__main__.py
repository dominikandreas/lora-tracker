"""Archiver command-line entry point."""

from __future__ import annotations

import argparse
import json
import logging
import os
from pathlib import Path
import sqlite3
import sys

from .config import ArchiverConfig
from .maintenance import (
    DatabaseProcessLock,
    backup_database,
    check_database,
    restore_database,
    retention_cutoff_ms,
)
from .service import ArchiverService, make_mqtt_client
from .store import HistoryStore


def _database_default() -> Path:
    return Path(os.getenv("DATABASE_PATH", "/data/lora-tracker-history.sqlite3"))


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="LoRa Tracker archive service and maintenance"
    )
    subcommands = parser.add_subparsers(dest="command")

    check = subcommands.add_parser(
        "check", help="verify SQLite and archive integrity"
    )
    check.add_argument("--database", type=Path, default=_database_default())

    backup = subcommands.add_parser("backup", help="create a consistent live backup")
    backup.add_argument("--database", type=Path, default=_database_default())
    backup.add_argument("--output", type=Path, required=True)
    backup.add_argument("--overwrite", action="store_true")

    restore = subcommands.add_parser(
        "restore", help="atomically restore a stopped archive"
    )
    restore.add_argument("--database", type=Path, default=_database_default())
    restore.add_argument("--input", type=Path, required=True)
    restore.add_argument("--force", action="store_true")

    prune = subcommands.add_parser(
        "prune", help="preview or apply retention pruning"
    )
    prune.add_argument("--database", type=Path, default=_database_default())
    prune.add_argument("--retention-days", type=int, required=True)
    prune.add_argument("--apply", action="store_true")
    return parser


def _run_maintenance(args: argparse.Namespace) -> int:
    if args.command == "check":
        report = check_database(args.database)
        print(json.dumps(report.to_dict(), sort_keys=True))
        return 0 if report.ok else 2
    if args.command == "backup":
        report = backup_database(
            args.database, args.output, overwrite=args.overwrite
        )
        print(json.dumps({"operation": "backup", **report.to_dict()}, sort_keys=True))
        return 0
    if args.command == "restore":
        report, pre_restore = restore_database(
            args.input, args.database, force=args.force
        )
        print(
            json.dumps(
                {
                    "operation": "restore",
                    "pre_restore_backup": str(pre_restore) if pre_restore else None,
                    **report.to_dict(),
                },
                sort_keys=True,
            )
        )
        return 0
    if args.command == "prune":
        cutoff_ms = retention_cutoff_ms(args.retention_days)
        if not args.database.is_file():
            raise FileNotFoundError(f"database does not exist: {args.database}")
        store = HistoryStore(args.database)
        try:
            matched = store.count_before(cutoff_ms)
            deleted = store.purge_before(cutoff_ms) if args.apply else 0
            if args.apply:
                store.checkpoint()
        finally:
            store.close()
        print(
            json.dumps(
                {
                    "operation": "prune",
                    "applied": args.apply,
                    "cutoff_unix_ms": cutoff_ms,
                    "matched": matched,
                    "deleted": deleted,
                },
                sort_keys=True,
            )
        )
        return 0
    return -1


def main() -> None:
    logging.basicConfig(
        level=os.getenv("LOG_LEVEL", "INFO").upper(),
        format="%(asctime)s %(levelname)s %(name)s: %(message)s",
    )
    args = _parser().parse_args()
    if args.command:
        try:
            status = _run_maintenance(args)
        except (OSError, RuntimeError, ValueError, sqlite3.Error) as exc:
            print(
                json.dumps(
                    {
                        "ok": False,
                        "operation": args.command,
                        "error": str(exc),
                    },
                    sort_keys=True,
                ),
                file=sys.stderr,
            )
            status = 2
        raise SystemExit(status)
    config = ArchiverConfig.from_env()
    with DatabaseProcessLock(config.database_path):
        ArchiverService(config, make_mqtt_client(config)).run()


if __name__ == "__main__":
    main()
