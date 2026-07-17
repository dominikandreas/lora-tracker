import { geoPosition } from './geometry.js';

export class SimulatorIntegration {
  constructor(mapManager) {
    this.mapManager = mapManager;
    this.worker = null;
    this.scenario = null;
    this.playing = false;
  }

  async init() {
    if (this.worker) return;
    const { createDefaultScenario } = await import('./default-scenario.js');
    this.scenario = createDefaultScenario();
    this.worker = new Worker(new URL('./simulator-worker.js', import.meta.url), { type: 'module' });

    this.worker.onmessage = ({ data }) => {
      if (data.type === 'snapshot') {
        this.handleSnapshot(data.snapshot);
      } else if (data.type === 'ready') {
        // We do not start automatically unless user presses play
      } else if (data.type === 'play-state') {
        this.playing = data.playing;
        document.getElementById('simPlay').textContent = this.playing ? 'Pause' : 'Run Simulation';
      }
    };
    
    this.worker.postMessage({ type: 'init', scenario: this.scenario });
  }

  terminate() {
    if (this.worker) {
      this.worker.terminate();
      this.worker = null;
    }
    this.playing = false;
    this.mapManager.clearSimulated();
  }

  togglePlay() {
    if (!this.worker) return;
    this.worker.postMessage({ type: this.playing ? 'pause' : 'play' });
  }

  handleSnapshot(snapshot) {
    if (!snapshot || !snapshot.devices) return;

    for (const device of snapshot.devices) {
      if (device.role === 'tracker') {
        const point = geoPosition(this.scenario, device);
        if (!point) continue;
        
        // Update marker
        this.mapManager.updateTracker(`sim_${device.id}`, point, `[SIM] ${device.name}`, true);
        
        // Draw waypoints/route if applicable
        if (device.waypoints && device.waypoints.length > 0) {
          const points = device.waypoints
            .map(w => geoPosition(this.scenario, w))
            .filter(Boolean);
          this.mapManager.drawRoute(`sim_route_${device.id}`, points, true);
        }
      }
    }
  }

  addEntity(entity) {
    this.worker.postMessage({ type: 'add-entity', entity });
  }

  reset() {
    this.worker.postMessage({ type: 'reset', scenario: this.scenario });
  }
}
