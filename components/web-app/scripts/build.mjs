import { access, cp, mkdir, readdir, writeFile } from "node:fs/promises";
import { resolve, relative, sep } from "node:path";
import { build } from "vite";

const root = resolve(import.meta.dirname, "..");
const dist = resolve(root, "dist");
const includeTestApi = process.argv.includes("--test");

await build({
  root,
  base: "./",
  publicDir: false,
  build: {
    outDir: dist,
    emptyOutDir: true,
    target: "es2022",
    rollupOptions: {
      input: includeTestApi
        ? {
            app: resolve(root, "index.html"),
            testApi: resolve(root, "test-api.js"),
          }
        : resolve(root, "index.html"),
      output: {
        entryFileNames: (chunk) =>
          chunk.name === "testApi"
            ? "test-api.js"
            : "assets/[name]-[hash].js",
      },
    },
  },
});

for (const file of ["manifest.webmanifest", "icon.svg"]) {
  await cp(resolve(root, file), resolve(dist, file));
}

// Keep the installed application self-contained by bundling the Network Lab.
const labSource = resolve(root, "../simulator-web/app");
try {
  await access(resolve(labSource, "index.html"));
  await mkdir(resolve(dist, "lab"), { recursive: true });
  await cp(labSource, resolve(dist, "lab"), { recursive: true });
} catch {
  throw new Error("Network Lab build missing; build components/simulator-web first");
}

async function listFiles(directory) {
  const result = [];
  for (const entry of await readdir(directory, { withFileTypes: true })) {
    const path = resolve(directory, entry.name);
    if (entry.isDirectory()) result.push(...(await listFiles(path)));
    else result.push(relative(dist, path).split(sep).join("/"));
  }
  return result;
}

const assets = (await listFiles(dist)).filter((file) => file !== "sw.js");
const serviceWorker = `const CACHE = "lora-tracker-web-${Date.now()}";
const ASSETS = ${JSON.stringify(["./", ...assets], null, 2)};
self.addEventListener("install", event => event.waitUntil(
  caches.open(CACHE).then(cache => cache.addAll(ASSETS)).then(() => self.skipWaiting())
));
self.addEventListener("activate", event => event.waitUntil(
  caches.keys().then(keys => Promise.all(keys.filter(key => key !== CACHE).map(key => caches.delete(key))))
    .then(() => self.clients.claim())
));
self.addEventListener("fetch", event => {
  if (event.request.method !== "GET" || new URL(event.request.url).origin !== self.location.origin) return;
  event.respondWith(fetch(event.request).then(response => {
    if (response.ok) caches.open(CACHE).then(cache => cache.put(event.request, response.clone()));
    return response;
  }).catch(async () => (await caches.match(event.request)) ||
    (event.request.mode === "navigate" ? caches.match("index.html") : Response.error())));
});
`;
await writeFile(resolve(dist, "sw.js"), serviceWorker);
