# Release Blockers Addressed

I have successfully resolved the 12 critical issues and release blockers you identified. The project's web-app branch is now stable, pushed, and has a reliable CI/CD pipeline.

## 1. Onboarding & BLE Controls
- Integrated the HTML UI in `index.html` for Tracker Onboarding (Claim, Auth, Config, Rollback, Reboot, Factory Reset).
- Corrected the `ROLLBACK <revision>` command dynamically using the previously fetched configuration revision.
- Fixed the `FACTORY_RESET FACTORY_RESET` command parameter according to firmware requirements.
- Addressed the timeout queue race condition in `onboarding.js`, avoiding synchronous array mutation anomalies on timeout/disconnect.

## 2. Infrastructure & Tests
- Hardened `ci.yml` to explicitly install the emscripten compiler, run `npm ci`, and build/copy assets in an isolated step, strictly evaluating `web-app` against the generated `simulation-engine` outputs.
- Prettier/dos2unix formatters were run, and `package-lock.json` files are properly versioned alongside `.gitignore` corrections (now tracking `dist/` explicitly to catch WASM drift securely via Git exit codes).
- The `ble.spec.js` suite now explicitly evaluates mock rejections on slow device writes and full timeout coverage.
- The `e2e.spec.js` suite includes new Content-Security-Policy evaluations utilizing console interceptors and fully tests offline service worker activation via Playwright context overrides.

## 3. UI/UX Enhancements
- Fixed UI fallback for `showOpenFilePicker()` utilizing a traditional `<input type="file" />` when the File System Access API is unavailable.
- Hidden `MapManager`'s production tracking nodes gracefully when swapping to "Network Lab" view.
- Adjusted alert thresholds correctly using the `missingGatewayThresholdMs` against individual tracker timelines, while protecting against zero-coordinate map exceptions (`latitude !== undefined`).
- Updated `ROADMAP.md` checkmarks properly matching incomplete CI requirements.

## Next Steps
The changes are fully committed and pushed. `npm test` runs cleanly on Playwright including all hardened specs. This branch is now ready for your review and possible merge into the production mainline!
