import { spawn } from "node:child_process";
import { resolve } from "node:path";

const root = resolve(import.meta.dirname, "..");
const server = spawn(
  process.execPath,
  [resolve(root, "scripts/serve.mjs"), "4173", "app"],
  { cwd: root, stdio: "inherit" },
);

function waitForExit(child, timeoutMs) {
  if (child.exitCode !== null || child.signalCode !== null) {
    return Promise.resolve(true);
  }
  return new Promise((resolveWait) => {
    const onExit = () => {
      clearTimeout(timer);
      resolveWait(true);
    };
    const timer = setTimeout(() => {
      child.off("exit", onExit);
      resolveWait(false);
    }, timeoutMs);
    child.once("exit", onExit);
  });
}

async function stopChild(child) {
  if (child.exitCode !== null || child.signalCode !== null) return;
  child.kill("SIGTERM");
  if (await waitForExit(child, 2_000)) return;
  child.kill("SIGKILL");
  await waitForExit(child, 2_000);
}

function waitForResult(child, timeoutMs) {
  return new Promise((resolveResult, reject) => {
    const onError = (error) => {
      clearTimeout(timer);
      child.off("exit", onExit);
      reject(error);
    };
    const onExit = (code) => {
      clearTimeout(timer);
      child.off("error", onError);
      resolveResult({ code: code ?? 1, timedOut: false });
    };
    const timer = setTimeout(() => {
      child.off("error", onError);
      child.off("exit", onExit);
      resolveResult({ code: 124, timedOut: true });
    }, timeoutMs);
    child.once("error", onError);
    child.once("exit", onExit);
  });
}

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
  const result = await waitForResult(playwright, 120_000);
  if (result.timedOut) {
    console.error("Playwright exceeded the 120-second browser-test deadline.");
    await stopChild(playwright);
  }
  if (result.code !== 0) process.exitCode = result.code;
} finally {
  await stopChild(server);
}
