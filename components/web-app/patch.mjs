import fs from "node:fs";
import path from "node:path";

const root = path.resolve(import.meta.dirname);

function walkBuildFiles(directory) {
  if (!fs.existsSync(directory)) return [];
  const results = [];
  for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
    if (entry.name === "build" || entry.name === ".gradle") continue;
    const file = path.join(directory, entry.name);
    if (entry.isDirectory()) results.push(...walkBuildFiles(file));
    else if (entry.name === "build.gradle") results.push(file);
  }
  return results;
}

// Capacitor can generate paths relative to the canonical target of a Windows
// junction while Gradle starts from the junction path. Those paths climb to C:\
// and make every plugin look like an empty project. Rebase only the generated
// node_modules suffix, which is stable on Windows and CI.
const settingsPath = path.join(root, "android", "capacitor.settings.gradle");
let settings = fs.readFileSync(settingsPath, "utf8");
settings = settings.replace(
  /new File\('(?:[^'\r\n]*[\\/])?node_modules[\\/]([^'\r\n]+)'\)/g,
  (_match, suffix) =>
    `new File('../node_modules/${suffix.replaceAll("\\", "/")}')`,
);
fs.writeFileSync(settingsPath, settings);

const moduleRoots = [
  path.join(root, "android"),
  path.join(root, "node_modules", "@capacitor", "android", "capacitor"),
  path.join(root, "node_modules", "@capacitor", "app", "android"),
  path.join(root, "node_modules", "@capacitor", "local-notifications", "android"),
  path.join(root, "node_modules", "@capacitor", "preferences", "android"),
  path.join(root, "node_modules", "@capacitor-community", "bluetooth-le", "android"),
  path.join(root, "node_modules", "@capacitor-community", "sqlite", "android"),
  path.join(root, "node_modules", "@aparajita", "capacitor-secure-storage", "android"),
];

for (const file of new Set(moduleRoots.flatMap(walkBuildFiles))) {
  let source = fs.readFileSync(file, "utf8");
  const updated = source
    .replaceAll("VERSION_21", "VERSION_17")
    .replace(/jvmTarget\s*=\s*["']21["']/g, 'jvmTarget = "17"')
    .replace(/jvmTarget\s*=\s*21/g, 'jvmTarget = "17"')
    .replaceAll("JvmTarget.JVM_21", "JvmTarget.JVM_17");
  if (updated !== source) fs.writeFileSync(file, updated);
}
