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

export async function putPoint(point) {
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

export async function listPoints(
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

export async function listLatestPoints() {
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

export async function clearPoints(deviceHash) {
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
