# Development history

`original-prototypes/` contains the latest user-supplied tracker and receiver
sources from before the multi-node/versioned-platform work. They are retained
for comparison and are not the recommended firmware.

`patches/` records the implementation milestones in chronological order:

1. Tracker power optimizations
2. Additional GNSS/movement/storage optimizations
3. Button/display fixes
4. BLE lifecycle/state-machine fix
5. LoRa retry and derived-speed fix
6. Protocol envelope and device identity
7. Multi-device gateway
8. Persistent configuration
9. Onboarding APIs
10. MQTT API and archiver
11. Delta timestamps and web app

Use the current sources under `components/` for new builds.
