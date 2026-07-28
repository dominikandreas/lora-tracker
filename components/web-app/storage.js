import { Capacitor } from "@capacitor/core";
import {
  CapacitorSQLite,
  SQLiteConnection,
} from "@capacitor-community/sqlite";

const DB_NAME = "lora-tracker-web";
const DB_VERSION = 3;
const STORE = "points";
const RETENTION_MS = 180 * 24 * 3600_000;
const MAX_POINTS = 250_000;
let lastPruneAt = 0;

function openDb() {
  return new Promise((resolve, reject) => {
    const request = indexedDB.open(DB_NAME, DB_VERSION);
    request.onupgradeneeded = () => {
      const db = request.result;
      const store = db.objectStoreNames.contains(STORE)
        ? request.transaction.objectStore(STORE)
        : db.createObjectStore(STORE, { keyPath: "point_id" });
      if (!store.indexNames.contains("device_time")) {
        store.createIndex("device_time", [
          "device_hash",
          "effective_time_unix_ms",
        ]);
      }
      if (!store.indexNames.contains("time")) {
        store.createIndex("time", "effective_time_unix_ms");
      }
      if (!store.indexNames.contains("device")) {
        store.createIndex("device", "device_hash");
      }
    };
    request.onsuccess = () => resolve(request.result);
    request.onerror = () => reject(request.error);
  });
}

async function deleteOldest(db, range, maximum) {
  await new Promise((resolve, reject) => {
    const tx = db.transaction(STORE, "readwrite");
    const request = tx.objectStore(STORE).index("time").openCursor(range);
    let deleted = 0;
    request.onsuccess = () => {
      const cursor = request.result;
      if (!cursor || deleted >= maximum) return;
      cursor.delete();
      deleted++;
      cursor.continue();
    };
    tx.oncomplete = resolve;
    tx.onerror = () => reject(tx.error);
  });
}

async function prunePoints() {
  const now = Date.now();
  if (now - lastPruneAt < 3600_000) return;
  lastPruneAt = now;
  const db = await openDb();
  try {
    await deleteOldest(
      db,
      IDBKeyRange.upperBound(now - RETENTION_MS, true),
      Number.MAX_SAFE_INTEGER,
    );
    const count = await new Promise((resolve, reject) => {
      const request = db
        .transaction(STORE, "readonly")
        .objectStore(STORE)
        .count();
      request.onsuccess = () => resolve(request.result);
      request.onerror = () => reject(request.error);
    });
    if (count > MAX_POINTS) {
      await deleteOldest(db, null, count - MAX_POINTS);
    }
  } finally {
    db.close();
  }
}

async function putPointBrowser(point) {
  const db = await openDb();
  await new Promise((resolve, reject) => {
    const tx = db.transaction(STORE, "readwrite");
    tx.objectStore(STORE).put(point);
    tx.oncomplete = resolve;
    tx.onerror = () => reject(tx.error);
  });
  db.close();
  prunePoints().catch(console.warn);
}

async function listPointsBrowser(
  deviceHash,
  fromMs = 0,
  toMs = Number.MAX_SAFE_INTEGER,
) {
  const db = await openDb();
  const result = await new Promise((resolve, reject) => {
    const tx = db.transaction(STORE, "readonly");
    const index = tx.objectStore(STORE).index("device_time");
    const range = IDBKeyRange.bound([deviceHash, fromMs], [deviceHash, toMs]);
    const request = index.getAll(range);
    request.onsuccess = () => resolve(request.result);
    request.onerror = () => reject(request.error);
  });
  db.close();
  return result.sort(
    (a, b) =>
      a.effective_time_unix_ms - b.effective_time_unix_ms || a.seq - b.seq,
  );
}

async function listLatestPointsBrowser() {
  const db = await openDb();
  try {
    const hashes = await new Promise((resolve, reject) => {
      const values = [];
      const request = db
        .transaction(STORE, "readonly")
        .objectStore(STORE)
        .index("device")
        .openKeyCursor(null, "nextunique");
      request.onsuccess = () => {
        const cursor = request.result;
        if (!cursor) return resolve(values);
        values.push(cursor.key);
        cursor.continue();
      };
      request.onerror = () => reject(request.error);
    });
    const points = await Promise.all(
      hashes.map(
        (hash) =>
          new Promise((resolve, reject) => {
            const request = db
              .transaction(STORE, "readonly")
              .objectStore(STORE)
              .index("device_time")
              .openCursor(
                IDBKeyRange.bound(
                  [hash, 0],
                  [hash, Number.MAX_SAFE_INTEGER],
                ),
                "prev",
              );
            request.onsuccess = () => resolve(request.result?.value || null);
            request.onerror = () => reject(request.error);
          }),
      ),
    );
    return points.filter(Boolean);
  } finally {
    db.close();
  }
}

async function clearPointsBrowser(deviceHash) {
  const db = await openDb();
  await new Promise((resolve, reject) => {
    const tx = db.transaction(STORE, "readwrite");
    const index = tx.objectStore(STORE).index("device_time");
    const range = IDBKeyRange.bound(
      [deviceHash, 0],
      [deviceHash, Number.MAX_SAFE_INTEGER],
    );
    const request = index.openKeyCursor(range);
    request.onsuccess = () => {
      const cursor = request.result;
      if (!cursor) return;
      tx.objectStore(STORE).delete(cursor.primaryKey);
      cursor.continue();
    };
    tx.oncomplete = resolve;
    tx.onerror = () => reject(tx.error);
  });
  db.close();
}

const useNativeSqlite = Capacitor.isNativePlatform();
let nativeDbPromise;

async function nativeDb() {
  if (!nativeDbPromise) {
    nativeDbPromise = (async () => {
      const manager = new SQLiteConnection(CapacitorSQLite);
      const db = await manager.createConnection(
        "lora_tracker",
        false,
        "no-encryption",
        1,
        false,
      );
      await db.open();
      await db.execute(`
        CREATE TABLE IF NOT EXISTS tracker_points (
          point_id TEXT PRIMARY KEY NOT NULL,
          device_hash TEXT NOT NULL,
          effective_time_unix_ms INTEGER NOT NULL,
          payload TEXT NOT NULL
        );
        CREATE INDEX IF NOT EXISTS idx_tracker_points_device_time
          ON tracker_points(device_hash, effective_time_unix_ms);
        CREATE INDEX IF NOT EXISTS idx_tracker_points_time
          ON tracker_points(effective_time_unix_ms);
      `);
      return db;
    })();
  }
  return nativeDbPromise;
}

async function pruneNativePoints(db) {
  const now = Date.now();
  if (now - lastPruneAt < 3600_000) return;
  lastPruneAt = now;
  await db.run(
    "DELETE FROM tracker_points WHERE effective_time_unix_ms < ?",
    [now - RETENTION_MS],
  );
  await db.run(
    `DELETE FROM tracker_points WHERE point_id IN (
       SELECT point_id FROM tracker_points
       ORDER BY effective_time_unix_ms DESC LIMIT -1 OFFSET ?
     )`,
    [MAX_POINTS],
  );
}

async function putPointNative(point) {
  const db = await nativeDb();
  await db.run(
    `INSERT OR REPLACE INTO tracker_points
      (point_id, device_hash, effective_time_unix_ms, payload)
      VALUES (?, ?, ?, ?)`,
    [
      String(point.point_id),
      point.device_hash,
      point.effective_time_unix_ms,
      JSON.stringify(point),
    ],
  );
  pruneNativePoints(db).catch(console.warn);
}

async function listPointsNative(deviceHash, fromMs, toMs) {
  const result = await (await nativeDb()).query(
    `SELECT payload FROM tracker_points
       WHERE device_hash = ? AND effective_time_unix_ms BETWEEN ? AND ?
       ORDER BY effective_time_unix_ms ASC`,
    [deviceHash, fromMs, toMs],
  );
  return (result.values || []).map((row) => JSON.parse(row.payload));
}

async function listLatestPointsNative() {
  const result = await (await nativeDb()).query(`
    SELECT payload FROM tracker_points AS point
    WHERE rowid = (
      SELECT rowid FROM tracker_points AS latest
      WHERE latest.device_hash = point.device_hash
      ORDER BY latest.effective_time_unix_ms DESC, latest.rowid DESC
      LIMIT 1
    )
  `);
  return (result.values || []).map((row) => JSON.parse(row.payload));
}

async function clearPointsNative(deviceHash) {
  await (await nativeDb()).run(
    "DELETE FROM tracker_points WHERE device_hash = ?",
    [deviceHash],
  );
}

export async function putPoint(point) {
  return useNativeSqlite ? putPointNative(point) : putPointBrowser(point);
}

export async function listPoints(
  deviceHash,
  fromMs = 0,
  toMs = Number.MAX_SAFE_INTEGER,
) {
  return useNativeSqlite
    ? listPointsNative(deviceHash, fromMs, toMs)
    : listPointsBrowser(deviceHash, fromMs, toMs);
}

export async function listLatestPoints() {
  return useNativeSqlite ? listLatestPointsNative() : listLatestPointsBrowser();
}

export async function clearPoints(deviceHash) {
  return useNativeSqlite
    ? clearPointsNative(deviceHash)
    : clearPointsBrowser(deviceHash);
}
