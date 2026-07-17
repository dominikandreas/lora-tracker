const CACHE = "lora-tracker-web-v5";
const ASSETS = [
  "./", "index.html", "styles.css", "app.js", "mqtt.js", "points.js", "storage.js", 
  "manifest.webmanifest", "map.js", "simulator-integration.js", "alerts.js", 
  "onboarding.js", "firmware-core.wasm", "simulator-worker.js", 
  "default-scenario.js", "geometry.js", "simulation-engine.js",
  "vendor/leaflet/leaflet.js", "vendor/leaflet/leaflet.css", "vendor/pmtiles/pmtiles.js"
];

self.addEventListener("install", event => {
  event.waitUntil(caches.open(CACHE).then(cache => cache.addAll(ASSETS)).then(() => self.skipWaiting()));
});

self.addEventListener("activate", event => {
  event.waitUntil(
    caches.keys()
      .then(keys => Promise.all(keys.filter(key => key !== CACHE).map(key => caches.delete(key))))
      .then(() => self.clients.claim())
  );
});

self.addEventListener("fetch", event => {
  if (event.request.method !== "GET" || new URL(event.request.url).origin !== self.location.origin) return;
  event.respondWith(
    fetch(event.request)
      .then(response => {
        if (response.ok) {
          const copy = response.clone();
          caches.open(CACHE).then(cache => cache.put(event.request, copy));
        }
        return response;
      })
      .catch(async () => {
        const cached = await caches.match(event.request);
        if (cached) return cached;
        if (event.request.mode === "navigate") return caches.match("index.html");
        return Response.error();
      })
  );
});
