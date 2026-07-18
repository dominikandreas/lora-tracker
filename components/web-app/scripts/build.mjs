import { cp, mkdir, rm } from "node:fs/promises";

await rm("vendor", { recursive: true, force: true });
await mkdir("vendor/leaflet", { recursive: true });
await mkdir("vendor/pmtiles", { recursive: true });
await cp("node_modules/leaflet/dist/leaflet.js", "vendor/leaflet/leaflet.js");
await cp("node_modules/leaflet/dist/leaflet.css", "vendor/leaflet/leaflet.css");
await cp("node_modules/leaflet/dist/images", "vendor/leaflet/images", {
  recursive: true,
});
await cp("node_modules/pmtiles/dist/pmtiles.js", "vendor/pmtiles/pmtiles.js");
