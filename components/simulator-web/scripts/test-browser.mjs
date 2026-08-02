import { spawn } from "node:child_process";
import { resolve } from "node:path";

const root = resolve(import.meta.dirname, "..");
const server = spawn(
  process.execPath,
  [resolve(root, "scripts/serve.mjs"), "4173", "app"],
  { cwd: root, stdio: "inherit" },
);

async function waitForServer() {
  for (let attempt = 0; attempt < 50; attempt += 1) {
    try {
      const response = await fetch("http://127.0.0.1:4173/");
      if (response.ok) return;
    } catch {}
    await new Promise((resolveWait) => setTimeout(resolveWait, 100));
  }
  throw new Error("Browser test server did not start");
}

try {
  await waitForServer();
  const playwright = spawn(
    process.execPath,
    [resolve(root, "node_modules/@playwright/test/cli.js"), "test"],
    {
      cwd: root,
      env: { ...process.env, PLAYWRIGHT_EXTERNAL_SERVER: "1" },
      stdio: "inherit",
    },
  );
  const exitCode = await new Promise((resolveExit, reject) => {
    playwright.once("error", reject);
    playwright.once("exit", (code) => resolveExit(code ?? 1));
  });
  if (exitCode !== 0) process.exitCode = exitCode;
} finally {
  server.kill();
}
