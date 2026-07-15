const DB_NAME = 'equine-web-v1';
const DB_VERSION = 1;
const STORE = 'points';

function openDb() {
  return new Promise((resolve, reject) => {
    const request = indexedDB.open(DB_NAME, DB_VERSION);
    request.onupgradeneeded = () => {
      const db = request.result;
      const store = db.createObjectStore(STORE, { keyPath: 'point_id' });
      store.createIndex('device_time', ['device_hash', 'effective_time_unix_ms']);
    };
    request.onsuccess = () => resolve(request.result);
    request.onerror = () => reject(request.error);
  });
}

export async function putPoint(point) {
  const db = await openDb();
  await new Promise((resolve, reject) => {
    const tx = db.transaction(STORE, 'readwrite');
    tx.objectStore(STORE).put(point);
    tx.oncomplete = resolve;
    tx.onerror = () => reject(tx.error);
  });
  db.close();
}

export async function listPoints(deviceHash, fromMs = 0, toMs = Number.MAX_SAFE_INTEGER) {
  const db = await openDb();
  const result = await new Promise((resolve, reject) => {
    const tx = db.transaction(STORE, 'readonly');
    const index = tx.objectStore(STORE).index('device_time');
    const range = IDBKeyRange.bound([deviceHash, fromMs], [deviceHash, toMs]);
    const request = index.getAll(range);
    request.onsuccess = () => resolve(request.result);
    request.onerror = () => reject(request.error);
  });
  db.close();
  return result.sort((a, b) => a.effective_time_unix_ms - b.effective_time_unix_ms || a.seq - b.seq);
}

export async function clearPoints(deviceHash) {
  const db = await openDb();
  await new Promise((resolve, reject) => {
    const tx = db.transaction(STORE, 'readwrite');
    const index = tx.objectStore(STORE).index('device_time');
    const range = IDBKeyRange.bound([deviceHash, 0], [deviceHash, Number.MAX_SAFE_INTEGER]);
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
