import { access } from "node:fs/promises";
import { resolve } from "node:path";

const dist = resolve(import.meta.dirname, "../dist");
for (const file of ["index.html", "sw.js", "manifest.webmanifest", "lab/index.html"]) {
  await access(resolve(dist, file));
}
console.log("Web/native bundle is complete.");
