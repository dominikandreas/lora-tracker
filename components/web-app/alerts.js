export class AlertsManager {
  constructor() {
    this.states = new Map(); // trackerHash -> { batteryState, lastSeen, alertLevel, etc }
    this.staleThresholdMs = 2 * 60 * 60 * 1000; // 2 hours
    this.missingGatewayThresholdMs = 15 * 60 * 1000; // 15 mins without a gateway update if expected
    this.enabled =
      "Notification" in window && Notification.permission === "granted";
    this.intervalId = null;
  }

  async requestPermission() {
    if (!("Notification" in window)) {
      alert("Web Notifications are not supported in this browser.");
      return false;
    }
    const perm = await Notification.requestPermission();
    this.enabled = perm === "granted";
    return this.enabled;
  }

  startInterval(trackersMap) {
    if (this.intervalId) clearInterval(this.intervalId);
    this.intervalId = setInterval(() => this.evaluateAll(trackersMap), 60000);
  }

  stopInterval() {
    if (this.intervalId) clearInterval(this.intervalId);
    this.intervalId = null;
  }

  getState(hash) {
    if (!this.states.has(hash)) {
      this.states.set(hash, {
        batteryState: 100, // assume full initially to avoid false alarms
        staleAlerted: false,
        missingAlerted: false,
        lastPosition: null,
      });
    }
    return this.states.get(hash);
  }

  evaluateTracker(tracker, latestPoint) {
    if (!latestPoint) return;
    const state = this.getState(tracker.hash);
    const now = Date.now();
    const age = now - latestPoint.effective_time_unix_ms;

    // Battery Hysteresis (20%, 10%, 5%)
    if (latestPoint.battery_level !== undefined) {
      const bat = latestPoint.battery_level;
      let newLevel = state.batteryState;

      if (bat <= 5) newLevel = 5;
      else if (bat <= 10) newLevel = 10;
      else if (bat <= 20) newLevel = 20;
      else if (bat > state.batteryState + 5) newLevel = bat; // Charging reset

      if (newLevel < state.batteryState && [20, 10, 5].includes(newLevel)) {
        this.notify(`Tracker ${tracker.name} battery is critical (${bat}%).`);
      }
      state.batteryState = newLevel;
    }

    // Unusual Movement
    if (
      state.lastPosition &&
      latestPoint.latitude !== undefined &&
      latestPoint.longitude !== undefined
    ) {
      const dt =
        (latestPoint.effective_time_unix_ms - state.lastPosition.time) / 1000;
      if (dt > 0 && dt < 3600) {
        // only evaluate if time diff is reasonable
        // Simple distance using lat/lon delta approx
        const dx =
          (latestPoint.longitude - state.lastPosition.lon) *
          111320 *
          Math.cos((latestPoint.latitude * Math.PI) / 180);
        const dy = (latestPoint.latitude - state.lastPosition.lat) * 110574;
        const dist = Math.sqrt(dx * dx + dy * dy);
        const speed = dist / dt; // m/s

        if (speed > 30) {
          // ~108 km/h is unusually fast for a horse
          this.notify(
            `Unusual movement detected for ${tracker.name} (${Math.round(speed * 3.6)} km/h).`,
          );
        }
      }
    }

    if (
      latestPoint.latitude !== undefined &&
      latestPoint.longitude !== undefined
    ) {
      state.lastPosition = {
        lat: latestPoint.latitude,
        lon: latestPoint.longitude,
        time: latestPoint.effective_time_unix_ms,
      };
    }

    if (latestPoint.gateway_id || latestPoint.gateway_hash) {
      state.lastGatewayTime = latestPoint.effective_time_unix_ms;
      state.missingAlerted = false;
    }

    // Reset staleness if fresh
    if (age < this.staleThresholdMs) state.staleAlerted = false;
  }

  evaluateAll(trackersMap) {
    const now = Date.now();
    for (const tracker of trackersMap.values()) {
      if (!tracker.latest) continue;
      const state = this.getState(tracker.hash);
      const age = now - tracker.latest.effective_time_unix_ms;

      if (age > this.staleThresholdMs && !state.staleAlerted) {
        this.notify(
          `Tracker ${tracker.name} is stale (last seen > 2 hours ago).`,
        );
        state.staleAlerted = true;
      }

      if (state.lastGatewayTime && !state.missingAlerted) {
        const gwAge = now - state.lastGatewayTime;
        if (gwAge > this.missingGatewayThresholdMs) {
          this.notify(
            `Tracker ${tracker.name} has not reached a gateway in > 15 mins.`,
          );
          state.missingAlerted = true;
        }
      }
    }
  }

  notify(message) {
    console.warn(`[ALERT] ${message}`);
    if (this.enabled) {
      new Notification("LoRa Tracker Alert", {
        body: message,
        icon: "manifest-icon-192.maskable.png",
      });
    }
  }
}
