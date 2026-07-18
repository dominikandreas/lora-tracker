import { cp, readdir } from "node:fs/promises";

for (const entry of await readdir("../simulation-engine/dist", {
  withFileTypes: true,
})) {
  if (entry.isFile()) {
    await cp(`../simulation-engine/dist/${entry.name}`, `app/${entry.name}`);
  }
}
