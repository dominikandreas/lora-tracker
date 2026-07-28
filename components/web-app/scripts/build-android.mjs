import { spawnSync } from "node:child_process";
import { resolve } from "node:path";

const root = resolve(import.meta.dirname, "..");

function run(command, args, options = {}) {
  const result = spawnSync(command, args, {
    cwd: root,
    stdio: "inherit",
    ...options,
  });
  if (result.error) throw result.error;
  if (result.status !== 0) process.exit(result.status ?? 1);
}

run(process.execPath, [process.env.npm_execpath, "run", "native:sync"]);

const windows = process.platform === "win32";
const androidRoot = resolve(root, "android");
const gradle = resolve(androidRoot, windows ? "gradlew.bat" : "gradlew");
if (windows) {
  run(process.env.ComSpec || "cmd.exe", [
    "/d",
    "/s",
    "/c",
    "gradlew.bat assembleDebug",
  ], { cwd: androidRoot });
} else {
  run(gradle, ["assembleDebug"], { cwd: androidRoot });
}
