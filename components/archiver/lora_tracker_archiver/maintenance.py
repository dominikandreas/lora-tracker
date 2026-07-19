"""Safe offline maintenance operations for the SQLite history archive."""

from __future__ import annotations

from contextlib import closing
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
import os
from pathlib import Path
import sqlite3
import tempfile
import time
from typing import BinaryIO


class DatabaseProcessLock:
    """Cross-platform advisory lock shared by service and destructive tools."""

    def __init__(self, database: Path | str):
        self.path = Path(f"{Path(database).expanduser().resolve()}.lock")
        self._handle: BinaryIO | None = None

    def __enter__(self) -> "DatabaseProcessLock":
        self.path.parent.mkdir(parents=True, exist_ok=True)
        handle = self.path.open("a+b")
        try:
            handle.seek(0, os.SEEK_END)
            if handle.tell() == 0:
                handle.write(b"\0")
                handle.flush()
            handle.seek(0)
            if os.name == "nt":
                import msvcrt

                msvcrt.locking(handle.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl

                fcntl.flock(handle.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except (OSError, BlockingIOError) as exc:
            handle.close()
            raise RuntimeError(
                f"archive is in use; stop the service before restore: {self.path}"
            ) from exc
        self._handle = handle
        return self

    def __exit__(self, exc_type: object, exc: object, traceback: object) -> None:
        if self._handle is None:
            return
        try:
            self._handle.seek(0)
            if os.name == "nt":
                import msvcrt

                msvcrt.locking(self._handle.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                import fcntl

                fcntl.flock(self._handle.fileno(), fcntl.LOCK_UN)
        finally:
            self._handle.close()
            self._handle = None


@dataclass(frozen=True)
class IntegrityReport:
    database: str
    ok: bool
    quick_check: tuple[str, ...]
    foreign_key_errors: tuple[tuple[object, ...], ...]
    points: int
    receptions: int

    def to_dict(self) -> dict[str, object]:
        return asdict(self)


def _require_database(path: Path | str) -> Path:
    resolved = Path(path).expanduser().resolve()
    if not resolved.is_file():
        raise FileNotFoundError(f"database does not exist: {resolved}")
    return resolved


def _read_only_connection(path: Path) -> sqlite3.Connection:
    return sqlite3.connect(f"{path.as_uri()}?mode=ro", uri=True, timeout=5)


def check_database(path: Path | str) -> IntegrityReport:
    source = _require_database(path)
    with closing(_read_only_connection(source)) as database:
        quick_check = tuple(
            str(row[0]) for row in database.execute("PRAGMA quick_check").fetchall()
        )
        foreign_key_errors = tuple(
            tuple(row) for row in database.execute("PRAGMA foreign_key_check").fetchall()
        )
        tables = {
            str(row[0])
            for row in database.execute(
                "SELECT name FROM sqlite_master WHERE type='table'"
            ).fetchall()
        }
        required = {"points", "receptions"}
        schema_ok = required <= tables
        points = (
            int(database.execute("SELECT COUNT(*) FROM points").fetchone()[0])
            if "points" in tables
            else 0
        )
        receptions = (
            int(database.execute("SELECT COUNT(*) FROM receptions").fetchone()[0])
            if "receptions" in tables
            else 0
        )
    return IntegrityReport(
        database=str(source),
        ok=quick_check == ("ok",) and not foreign_key_errors and schema_ok,
        quick_check=quick_check if schema_ok else quick_check + ("missing archive tables",),
        foreign_key_errors=foreign_key_errors,
        points=points,
        receptions=receptions,
    )


def _temporary_database(destination: Path) -> Path:
    destination.parent.mkdir(parents=True, exist_ok=True)
    descriptor, name = tempfile.mkstemp(
        prefix=f".{destination.name}.", suffix=".tmp", dir=destination.parent
    )
    os.close(descriptor)
    return Path(name)


def _remove_sidecars(database: Path) -> None:
    for suffix in ("-wal", "-shm"):
        Path(f"{database}{suffix}").unlink(missing_ok=True)


def _copy_database(source: Path, destination: Path) -> None:
    temporary = _temporary_database(destination)
    try:
        with closing(_read_only_connection(source)) as source_db, closing(
            sqlite3.connect(temporary)
        ) as destination_db:
            with destination_db:
                source_db.backup(destination_db)
                destination_db.execute("PRAGMA foreign_keys=ON")
        report = check_database(temporary)
        if not report.ok:
            raise RuntimeError(f"created database failed integrity checks: {report}")
        # Windows requires a writable file handle for FlushFileBuffers, which
        # backs Python's fsync implementation there.
        with temporary.open("r+b") as handle:
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary, destination)
        _remove_sidecars(destination)
    finally:
        temporary.unlink(missing_ok=True)


def backup_database(
    source: Path | str, destination: Path | str, *, overwrite: bool = False
) -> IntegrityReport:
    source_path = _require_database(source)
    destination_path = Path(destination).expanduser().resolve()
    if source_path == destination_path:
        raise ValueError("backup destination must differ from the live database")
    if destination_path.exists() and not overwrite:
        raise FileExistsError(f"backup already exists: {destination_path}")
    _copy_database(source_path, destination_path)
    report = check_database(destination_path)
    _remove_sidecars(destination_path)
    return report


def _pre_restore_path(database: Path) -> Path:
    timestamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    candidate = database.with_name(f"{database.name}.pre-restore-{timestamp}")
    suffix = 1
    while candidate.exists():
        candidate = database.with_name(
            f"{database.name}.pre-restore-{timestamp}-{suffix}"
        )
        suffix += 1
    return candidate


def restore_database(
    source: Path | str, destination: Path | str, *, force: bool = False
) -> tuple[IntegrityReport, Path | None]:
    source_path = _require_database(source)
    source_report = check_database(source_path)
    if not source_report.ok:
        raise RuntimeError("refusing to restore an invalid archive")

    destination_path = Path(destination).expanduser().resolve()
    if source_path == destination_path:
        raise ValueError("restore source must differ from the live database")
    with DatabaseProcessLock(destination_path):
        pre_restore: Path | None = None
        if destination_path.exists():
            if not force:
                raise FileExistsError(
                    "live database exists; stop the service and pass --force to restore"
                )
            pre_restore = _pre_restore_path(destination_path)
            backup_database(destination_path, pre_restore)
        elif not destination_path.parent.exists():
            destination_path.parent.mkdir(parents=True)

        _copy_database(source_path, destination_path)
        # Remove stale sidecars from the replaced inode so SQLite cannot replay
        # an unrelated pre-restore WAL.
        _remove_sidecars(destination_path)
        report = check_database(destination_path)
        _remove_sidecars(destination_path)
        return report, pre_restore


def retention_cutoff_ms(retention_days: int, *, now_s: float | None = None) -> int:
    if not 1 <= retention_days <= 3650:
        raise ValueError("retention days must be between 1 and 3650")
    current_s = time.time() if now_s is None else now_s
    return int((current_s - retention_days * 86400) * 1000)
