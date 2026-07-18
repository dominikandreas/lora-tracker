import { cp, mkdir, rm } from "node:fs/promises";

const files = [
  "index.html",
  "styles.css",
  "app.js",
  "mqtt.js",
  "points.js",
  "storage.js",
  "map.js",
  "alerts.js",
  "onboarding.js",
  "sw.js",
  "manifest.webmanifest",
  "icon.svg",
];

await rm("dist", { recursive: true, force: true });
await mkdir("dist", { recursive: true });
for (const file of files) await cp(file, `dist/${file}`);
await cp("vendor", "dist/vendor", { recursive: true });
